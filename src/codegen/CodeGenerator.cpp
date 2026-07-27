#include "CodeGenerator.h"
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Constants.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>
#include <cstdlib>
#include <stdexcept>
#include <sstream>
#include <fstream>

namespace arc::codegen {

CodeGenerator::CodeGenerator()
    : context(std::make_unique<llvm::LLVMContext>()),
      module(std::make_unique<llvm::Module>("arc_module", *context)),
      builder(std::make_unique<llvm::IRBuilder<>>(*context)),
      targetMachine(nullptr),
      currentFunction(nullptr),
      lastValue(nullptr) {

    std::string triple = "x86_64-pc-linux-gnu";
    module->setTargetTriple(triple);
    module->setDataLayout("e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128");
    declareBuiltins();
}

CodeGenerator::~CodeGenerator() {
}

bool CodeGenerator::generate(arc::ast::Program* program, const std::string& outputPath) {
    try {
        program->accept(this);

        if (llvm::verifyModule(*module, &llvm::errs())) {
            return false;
        }

        return emitObject(outputPath);
    } catch (const std::exception& e) {
        llvm::errs() << "Codegen Error: " << e.what() << "\n";
        return false;
    }
}

bool CodeGenerator::emitObject(const std::string& outputPath) {
    std::string llFile = outputPath + ".ll";
    std::error_code ec;
    llvm::raw_fd_ostream llStream(llFile, ec);
    if (ec) {
        llvm::errs() << "Could not open file for writing: " << llFile << "\n";
        return false;
    }

    module->print(llStream, nullptr);
    llStream.flush();

    std::string llcCommand = "llc -filetype=obj -o " + outputPath + " " + llFile;
    int result = std::system(llcCommand.c_str());

    if (result != 0) {
        llvm::errs() << "llc compilation failed\n";
        return false;
    }

    return true;
}

bool CodeGenerator::linkExecutable(const std::string& objectFile,
                                    const std::string& executable) {
    std::string command = "gcc -o " + executable + " " + objectFile;

    int result = std::system(command.c_str());
    return result == 0;
}

arc::ast::Type CodeGenerator::derefType(const arc::ast::Type& type) {
    arc::ast::Type result = type;
    if (result.pointerLevel > 0) {
        result.pointerLevel--;
    } else {
        throw std::runtime_error("Cannot dereference non-pointer type");
    }
    return result;
}

llvm::Type* CodeGenerator::arcTypeToLLVM(const arc::ast::Type& type) {
    llvm::Type* baseType;

    if (type.isStruct()) {
        auto it = structTypes.find(type.structName);
        if (it != structTypes.end()) {
            baseType = it->second;
        } else {
            throw std::runtime_error("Unknown struct type: " + type.structName);
        }
    } else {
        switch (type.base) {
            case arc::ast::PrimitiveType::INT:
                baseType = llvm::Type::getInt32Ty(*context);
                break;
            case arc::ast::PrimitiveType::FLOAT:
                baseType = llvm::Type::getFloatTy(*context);
                break;
            case arc::ast::PrimitiveType::DOUBLE:
                baseType = llvm::Type::getDoubleTy(*context);
                break;
            case arc::ast::PrimitiveType::BOOL:
                baseType = llvm::Type::getInt1Ty(*context);
                break;
            case arc::ast::PrimitiveType::CHAR:
                baseType = llvm::Type::getInt8Ty(*context);
                break;
            case arc::ast::PrimitiveType::VOID:
                baseType = llvm::Type::getVoidTy(*context);
                break;
            default:
                throw std::runtime_error("Unknown type");
        }
    }

    for (int i = 0; i < type.pointerLevel; ++i) {
        baseType = llvm::PointerType::get(baseType, 0);
    }

    return baseType;
}

llvm::FunctionType* CodeGenerator::createFunctionType(
    const arc::ast::FunctionDecl* decl) {

    std::vector<llvm::Type*> paramTypes;
    for (const auto& param : decl->parameters) {
        paramTypes.push_back(arcTypeToLLVM(param.type));
    }

    llvm::Type* returnType = arcTypeToLLVM(decl->returnType);
    return llvm::FunctionType::get(returnType, paramTypes, false);
}

void CodeGenerator::declareBuiltins() {
    llvm::Type* i8Ptr = llvm::PointerType::get(llvm::Type::getInt8Ty(*context), 0);
    llvm::Type* i32 = llvm::Type::getInt32Ty(*context);
    llvm::Type* i64 = llvm::Type::getInt64Ty(*context);
    llvm::Type* voidTy = llvm::Type::getVoidTy(*context);

    {
        std::vector<llvm::Type*> args = {i8Ptr};
        auto funcType = llvm::FunctionType::get(i32, args, true);
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                              "printf", module.get());
    }

    {
        std::vector<llvm::Type*> args = {i8Ptr, i8Ptr};
        auto funcType = llvm::FunctionType::get(i8Ptr, args, false);
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                              "fopen", module.get());
    }

    {
        std::vector<llvm::Type*> args = {i8Ptr};
        auto funcType = llvm::FunctionType::get(i32, args, false);
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                              "fclose", module.get());
    }

    {
        std::vector<llvm::Type*> args = {i8Ptr, i64, i64, i8Ptr};
        auto funcType = llvm::FunctionType::get(i64, args, false);
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                              "fread", module.get());
    }

    {
        std::vector<llvm::Type*> args = {i8Ptr, i64, i32};
        auto funcType = llvm::FunctionType::get(i32, args, false);
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                              "fseek", module.get());
    }

    {
        std::vector<llvm::Type*> args = {i8Ptr};
        auto funcType = llvm::FunctionType::get(i64, args, false);
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                              "ftell", module.get());
    }

    {
        std::vector<llvm::Type*> args = {i32};
        auto funcType = llvm::FunctionType::get(i8Ptr, args, false);
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                              "malloc", module.get());
    }

    {
        std::vector<llvm::Type*> args = {i8Ptr};
        auto funcType = llvm::FunctionType::get(voidTy, args, false);
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                              "free", module.get());
    }
}

void CodeGenerator::pushScope() {
    scopeStack.push(variables);
    typeStack.push(variableTypes);
    arcTypeStack.push(arcVariableTypes);
}

void CodeGenerator::popScope() {
    if (!scopeStack.empty()) {
        variables = scopeStack.top();
        scopeStack.pop();
        variableTypes = typeStack.top();
        typeStack.pop();
        arcVariableTypes = arcTypeStack.top();
        arcTypeStack.pop();
    }
}

void CodeGenerator::setVariable(const std::string& name, llvm::Value* value) {
    variables[name] = value;
}

llvm::Value* CodeGenerator::getVariable(const std::string& name) {
    auto it = variables.find(name);
    if (it == variables.end()) {
        throw std::runtime_error("Undefined variable: " + name);
    }
    return it->second;
}

void CodeGenerator::visit(arc::ast::Literal* node) {
    switch (node->kind) {
        case arc::ast::Literal::Kind::INTEGER: {
            int value = std::stoi(node->value);
            lastValue = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), value);
            break;
        }
        case arc::ast::Literal::Kind::FLOAT: {
            float value = std::stof(node->value);
            lastValue = llvm::ConstantFP::get(llvm::Type::getFloatTy(*context), value);
            break;
        }
        case arc::ast::Literal::Kind::STRING: {
            lastValue = builder->CreateGlobalString(node->value);
            break;
        }
        case arc::ast::Literal::Kind::BOOLEAN: {
            bool value = (node->value == "true");
            lastValue = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), value);
            break;
        }
    }
}

void CodeGenerator::visit(arc::ast::Identifier* node) {
    llvm::Value* varPtr = getVariable(node->name);
    auto it = variableTypes.find(node->name);
    if (it != variableTypes.end()) {
        lastValue = builder->CreateLoad(it->second, varPtr);
    } else {
        lastValue = varPtr;
    }
}

void CodeGenerator::visit(arc::ast::BinaryOp* node) {
    if (node->op == arc::ast::BinaryOp::Operator::ASSIGN) {
        node->right->accept(this);
        llvm::Value* rhsValue = lastValue;

        if (auto* ident = dynamic_cast<arc::ast::Identifier*>(node->left.get())) {
            llvm::Value* lhsAddr = getVariable(ident->name);
            builder->CreateStore(rhsValue, lhsAddr);
            lastValue = rhsValue;
        } else if (auto* arrayAccess = dynamic_cast<arc::ast::ArrayAccess*>(node->left.get())) {
            arrayAccess->array->accept(this);
            llvm::Value* arrayPtr = lastValue;

            arrayAccess->index->accept(this);
            llvm::Value* indexVal = lastValue;

            auto arcTypeIt = arcVariableTypes.find(
                dynamic_cast<arc::ast::Identifier*>(arrayAccess->array.get()) ?
                dynamic_cast<arc::ast::Identifier*>(arrayAccess->array.get())->name : ""
            );

            if (arcTypeIt != arcVariableTypes.end()) {
                arc::ast::Type elemType = derefType(arcTypeIt->second);
                llvm::Type* elemLLVMType = arcTypeToLLVM(elemType);
                llvm::Value* elemPtr = builder->CreateGEP(elemLLVMType, arrayPtr, indexVal);
                builder->CreateStore(rhsValue, elemPtr);
                lastValue = rhsValue;
            } else {
                throw std::runtime_error("Cannot determine array element type");
            }
        } else {
            throw std::runtime_error("Invalid assignment target");
        }
        return;
    }

    node->left->accept(this);
    llvm::Value* lhs = lastValue;

    node->right->accept(this);
    llvm::Value* rhs = lastValue;

    if (lhs->getType()->isPointerTy() && rhs->getType()->isIntegerTy()) {
        if (auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(rhs)) {
            if (constInt->isZero()) {
                rhs = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(lhs->getType()));
            }
        }
    } else if (rhs->getType()->isPointerTy() && lhs->getType()->isIntegerTy()) {
        if (auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(lhs)) {
            if (constInt->isZero()) {
                lhs = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(rhs->getType()));
            }
        }
    }

    switch (node->op) {
        case arc::ast::BinaryOp::Operator::PLUS:
            lastValue = builder->CreateAdd(lhs, rhs);
            break;
        case arc::ast::BinaryOp::Operator::MINUS:
            lastValue = builder->CreateSub(lhs, rhs);
            break;
        case arc::ast::BinaryOp::Operator::MULTIPLY:
            lastValue = builder->CreateMul(lhs, rhs);
            break;
        case arc::ast::BinaryOp::Operator::DIVIDE:
            lastValue = builder->CreateSDiv(lhs, rhs);
            break;
        case arc::ast::BinaryOp::Operator::MODULO:
            lastValue = builder->CreateSRem(lhs, rhs);
            break;
        case arc::ast::BinaryOp::Operator::EQUAL:
            lastValue = builder->CreateICmpEQ(lhs, rhs);
            break;
        case arc::ast::BinaryOp::Operator::NOT_EQUAL:
            lastValue = builder->CreateICmpNE(lhs, rhs);
            break;
        case arc::ast::BinaryOp::Operator::LESS:
            lastValue = builder->CreateICmpSLT(lhs, rhs);
            break;
        case arc::ast::BinaryOp::Operator::LESS_EQUAL:
            lastValue = builder->CreateICmpSLE(lhs, rhs);
            break;
        case arc::ast::BinaryOp::Operator::GREATER:
            lastValue = builder->CreateICmpSGT(lhs, rhs);
            break;
        case arc::ast::BinaryOp::Operator::GREATER_EQUAL:
            lastValue = builder->CreateICmpSGE(lhs, rhs);
            break;
        case arc::ast::BinaryOp::Operator::LOGICAL_AND:
            lastValue = builder->CreateAnd(lhs, rhs);
            break;
        case arc::ast::BinaryOp::Operator::LOGICAL_OR:
            lastValue = builder->CreateOr(lhs, rhs);
            break;
        case arc::ast::BinaryOp::Operator::BITWISE_AND:
            lastValue = builder->CreateAnd(lhs, rhs);
            break;
        case arc::ast::BinaryOp::Operator::BITWISE_OR:
            lastValue = builder->CreateOr(lhs, rhs);
            break;
        case arc::ast::BinaryOp::Operator::BITWISE_XOR:
            lastValue = builder->CreateXor(lhs, rhs);
            break;
        default:
            throw std::runtime_error("Unknown binary operator");
    }
}

void CodeGenerator::visit(arc::ast::UnaryOp* node) {
    node->operand->accept(this);
    llvm::Value* operand = lastValue;

    switch (node->op) {
        case arc::ast::UnaryOp::Operator::NEGATE:
            lastValue = builder->CreateNeg(operand);
            break;
        case arc::ast::UnaryOp::Operator::NOT:
            lastValue = builder->CreateNot(operand);
            break;
        case arc::ast::UnaryOp::Operator::BITWISE_NOT:
            lastValue = builder->CreateNot(operand);
            break;
        case arc::ast::UnaryOp::Operator::DEREF: {
            if (auto* ident = dynamic_cast<arc::ast::Identifier*>(node->operand.get())) {
                auto arcTypeIt = arcVariableTypes.find(ident->name);
                if (arcTypeIt == arcVariableTypes.end()) {
                    throw std::runtime_error("Unknown variable: " + ident->name);
                }

                arc::ast::Type derefArcType = derefType(arcTypeIt->second);
                llvm::Type* derefLLVMType = arcTypeToLLVM(derefArcType);

                llvm::Value* ptrAddr = getVariable(ident->name);
                auto llvmTypeIt = variableTypes.find(ident->name);
                llvm::Type* ptrLLVMType = llvmTypeIt->second;

                llvm::Value* ptrVal = builder->CreateLoad(ptrLLVMType, ptrAddr);
                lastValue = builder->CreateLoad(derefLLVMType, ptrVal);
            } else {
                node->operand->accept(this);
                lastValue = builder->CreateLoad(
                    llvm::Type::getInt8Ty(*context),
                    lastValue
                );
            }
            break;
        }
        case arc::ast::UnaryOp::Operator::ADDRESS_OF: {
            if (auto* ident = dynamic_cast<arc::ast::Identifier*>(node->operand.get())) {
                lastValue = getVariable(ident->name);
            } else {
                throw std::runtime_error("Cannot take address of non-lvalue");
            }
            break;
        }
    }
}

void CodeGenerator::visit(arc::ast::FunctionCall* node) {
    llvm::Function* func = module->getFunction(node->functionName);
    if (!func) {
        throw std::runtime_error("Undefined function: " + node->functionName);
    }

    std::vector<llvm::Value*> args;
    llvm::FunctionType* funcType = func->getFunctionType();

    for (size_t i = 0; i < node->arguments.size(); ++i) {
        node->arguments[i]->accept(this);
        llvm::Value* argValue = lastValue;

        if (i < funcType->getNumParams()) {
            llvm::Type* paramType = funcType->getParamType(i);
            if (argValue->getType() != paramType) {
                if (argValue->getType()->isIntegerTy() && paramType->isIntegerTy()) {
                    argValue = builder->CreateIntCast(argValue, paramType, true);
                } else if (argValue->getType()->isIntegerTy() && paramType->isPointerTy()) {
                    if (auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(argValue)) {
                        if (constInt->isZero()) {
                            argValue = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(paramType));
                        }
                    }
                }
            }
        }
        args.push_back(argValue);
    }

    lastValue = builder->CreateCall(func, args);
}

void CodeGenerator::visit(arc::ast::MemberAccess* node) {
    node->object->accept(this);
    llvm::Value* objectPtr = lastValue;

    if (auto* ident = dynamic_cast<arc::ast::Identifier*>(node->object.get())) {
        auto it = variableTypes.find(ident->name);
        if (it == variableTypes.end()) {
            throw std::runtime_error("Unknown variable type: " + ident->name);
        }

        llvm::Type* objectType = it->second;
        llvm::StructType* structType = nullptr;

        if (auto* st = llvm::dyn_cast<llvm::StructType>(objectType)) {
            structType = st;
        } else if (auto* pt = llvm::dyn_cast<llvm::PointerType>(objectType)) {
            (void)pt;
            auto arcIt = arcVariableTypes.find(ident->name);
            if (arcIt != arcVariableTypes.end()) {
                arc::ast::Type elemType = this->derefType(arcIt->second);
                if (elemType.isStruct()) {
                    auto structTypeIt = structTypes.find(elemType.structName);
                    if (structTypeIt != structTypes.end()) {
                        structType = structTypeIt->second;
                    }
                }
            }
        }

        if (structType) {
            auto structIt = structFields.find(structType->getName().str());
            if (structIt == structFields.end()) {
                throw std::runtime_error("Unknown struct type: " + structType->getName().str());
            }

            unsigned fieldIndex = 0;
            for (unsigned i = 0; i < structIt->second.size(); ++i) {
                if (structIt->second[i] == node->member) {
                    fieldIndex = i;
                    break;
                }
            }

            llvm::Value* elementPtr = builder->CreateStructGEP(structType, objectPtr, fieldIndex);
            lastValue = builder->CreateLoad(structType->getElementType(fieldIndex), elementPtr);
        } else {
            throw std::runtime_error("Object is not a struct type");
        }
    } else {
        throw std::runtime_error("Invalid member access");
    }
}

void CodeGenerator::visit(arc::ast::ArrayAccess* node) {
    node->array->accept(this);
    llvm::Value* arrayPtr = lastValue;

    node->index->accept(this);
    llvm::Value* indexVal = lastValue;

    auto arcTypeIt = arcVariableTypes.find(
        dynamic_cast<arc::ast::Identifier*>(node->array.get()) ?
        dynamic_cast<arc::ast::Identifier*>(node->array.get())->name : ""
    );

    if (arcTypeIt != arcVariableTypes.end()) {
        arc::ast::Type elemType = derefType(arcTypeIt->second);
        llvm::Type* elemLLVMType = arcTypeToLLVM(elemType);
        llvm::Value* elemPtr = builder->CreateGEP(elemLLVMType, arrayPtr, indexVal);
        lastValue = builder->CreateLoad(elemLLVMType, elemPtr);
    } else {
        throw std::runtime_error("Cannot determine array element type");
    }
}

void CodeGenerator::visit(arc::ast::VariableDecl* node) {
    llvm::Type* type = arcTypeToLLVM(node->type);
    llvm::AllocaInst* alloca = builder->CreateAlloca(type);

    if (node->initializer) {
        node->initializer->accept(this);
        builder->CreateStore(lastValue, alloca);
    }

    setVariable(node->name, alloca);
    variableTypes[node->name] = type;
    arcVariableTypes[node->name] = node->type;
}

void CodeGenerator::visit(arc::ast::ReturnStmt* node) {
    if (node->value) {
        node->value->accept(this);
        llvm::Value* retVal = lastValue;

        if (currentFunction) {
            llvm::Type* expectedType = currentFunction->getReturnType();
            if (retVal->getType() != expectedType) {
                if (retVal->getType()->isIntegerTy() && expectedType->isIntegerTy()) {
                    retVal = builder->CreateIntCast(retVal, expectedType, true);
                } else if (retVal->getType()->isFloatingPointTy() && expectedType->isFloatingPointTy()) {
                    retVal = builder->CreateFPCast(retVal, expectedType);
                }
            }
        }

        builder->CreateRet(retVal);
    } else {
        builder->CreateRetVoid();
    }
}

void CodeGenerator::visit(arc::ast::ExpressionStmt* node) {
    node->expression->accept(this);
}

void CodeGenerator::visit(arc::ast::Block* node) {
    for (const auto& stmt : node->statements) {
        stmt->accept(this);
    }
}

void CodeGenerator::visit(arc::ast::IfStmt* node) {
    node->condition->accept(this);
    llvm::Value* condition = lastValue;

    llvm::BasicBlock* thenBlock = llvm::BasicBlock::Create(*context, "then", currentFunction);
    llvm::BasicBlock* elseBlock = nullptr;
    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(*context, "ifend", currentFunction);

    if (node->elseBranch) {
        elseBlock = llvm::BasicBlock::Create(*context, "else", currentFunction);
        builder->CreateCondBr(condition, thenBlock, elseBlock);
    } else {
        builder->CreateCondBr(condition, thenBlock, mergeBlock);
    }

    builder->SetInsertPoint(thenBlock);
    node->thenBranch->accept(this);
    if (!llvm::isa<llvm::ReturnInst>(builder->GetInsertBlock()->back())) {
        builder->CreateBr(mergeBlock);
    }

    if (node->elseBranch) {
        builder->SetInsertPoint(elseBlock);
        node->elseBranch->accept(this);
        if (!llvm::isa<llvm::ReturnInst>(builder->GetInsertBlock()->back())) {
            builder->CreateBr(mergeBlock);
        }
    }

    builder->SetInsertPoint(mergeBlock);
}

void CodeGenerator::visit(arc::ast::ForStmt* node) {
    llvm::BasicBlock* loopBlock = llvm::BasicBlock::Create(*context, "forloop", currentFunction);
    llvm::BasicBlock* afterBlock = llvm::BasicBlock::Create(*context, "forend", currentFunction);

    pushScope();

    if (node->init) {
        node->init->accept(this);
    }

    builder->CreateBr(loopBlock);
    builder->SetInsertPoint(loopBlock);

    if (node->condition) {
        node->condition->accept(this);
        builder->CreateCondBr(lastValue, loopBlock, afterBlock);
    }

    llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(*context, "forbody", currentFunction);
    builder->SetInsertPoint(bodyBlock);
    node->body->accept(this);

    if (node->increment) {
        node->increment->accept(this);
    }

    builder->CreateBr(loopBlock);
    builder->SetInsertPoint(afterBlock);

    popScope();
}

void CodeGenerator::visit(arc::ast::WhileStmt* node) {
    llvm::BasicBlock* condBlock = llvm::BasicBlock::Create(*context, "whilecond", currentFunction);
    llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(*context, "whilebody", currentFunction);
    llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(*context, "whileend", currentFunction);

    builder->CreateBr(condBlock);
    builder->SetInsertPoint(condBlock);

    node->condition->accept(this);
    builder->CreateCondBr(lastValue, bodyBlock, endBlock);

    builder->SetInsertPoint(bodyBlock);
    node->body->accept(this);
    builder->CreateBr(condBlock);

    builder->SetInsertPoint(endBlock);
}

void CodeGenerator::visit(arc::ast::FunctionDecl* node) {
    llvm::FunctionType* funcType = createFunctionType(node);
    llvm::Function* function = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, node->name, module.get()
    );

    llvm::BasicBlock* entryBlock = llvm::BasicBlock::Create(*context, "entry", function);
    builder->SetInsertPoint(entryBlock);

    currentFunction = function;
    variables.clear();

    auto argIt = function->arg_begin();
    for (const auto& param : node->parameters) {
        argIt->setName(param.name);
        llvm::Type* paramType = arcTypeToLLVM(param.type);
        llvm::AllocaInst* alloca = builder->CreateAlloca(paramType);
        builder->CreateStore(&*argIt, alloca);
        setVariable(param.name, alloca);
        arcVariableTypes[param.name] = param.type;
        variableTypes[param.name] = paramType;
        ++argIt;
    }

    if (node->body) {
        node->body->accept(this);
    }

    if (node->returnType.isVoid() &&
        !llvm::isa<llvm::ReturnInst>(builder->GetInsertBlock()->back())) {
        builder->CreateRetVoid();
    }
}

void CodeGenerator::visit(arc::ast::StructDecl* node) {
    std::vector<llvm::Type*> fieldTypes;
    std::vector<std::string> fieldNames;

    for (const auto& field : node->fields) {
        fieldTypes.push_back(arcTypeToLLVM(field.type));
        fieldNames.push_back(field.name);
    }

    llvm::StructType* structType = llvm::StructType::create(*context, fieldTypes, node->name);
    structTypes[node->name] = structType;
    structFields[node->name] = fieldNames;
}

void CodeGenerator::visit(arc::ast::Program* node) {
    for (const auto& strct : node->structs) {
        strct->accept(this);
    }

    for (const auto& func : node->functions) {
        func->accept(this);
    }
}

} // namespace arc::codegen

