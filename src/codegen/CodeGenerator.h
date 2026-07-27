#pragma once

#include "ast/ASTNode.h"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/Target/TargetMachine.h>
#include <memory>
#include <unordered_map>
#include <stack>
#include <string>

namespace arc::codegen {

class CodeGenerator : public arc::ast::ASTVisitor {
public:
    CodeGenerator();
    ~CodeGenerator();

    bool generate(arc::ast::Program* program, const std::string& outputPath);
    bool emitObject(const std::string& outputPath);
    bool linkExecutable(const std::string& objectFile, const std::string& executable);

private:
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;
    llvm::TargetMachine* targetMachine;

    std::unordered_map<std::string, llvm::Value*> variables;
    std::unordered_map<std::string, llvm::Type*> variableTypes;
    std::unordered_map<std::string, arc::ast::Type> arcVariableTypes;
    std::stack<std::unordered_map<std::string, llvm::Value*>> scopeStack;
    std::stack<std::unordered_map<std::string, llvm::Type*>> typeStack;
    std::stack<std::unordered_map<std::string, arc::ast::Type>> arcTypeStack;
    std::unordered_map<std::string, llvm::StructType*> structTypes;
    std::unordered_map<std::string, std::vector<std::string>> structFields;
    llvm::Function* currentFunction;
    llvm::Value* lastValue;

    llvm::Type* arcTypeToLLVM(const arc::ast::Type& type);
    arc::ast::Type derefType(const arc::ast::Type& type);
    llvm::FunctionType* createFunctionType(const arc::ast::FunctionDecl* decl);
    void declareBuiltins();
    void pushScope();
    void popScope();
    void setVariable(const std::string& name, llvm::Value* value);
    llvm::Value* getVariable(const std::string& name);

    void visit(arc::ast::Literal* node) override;
    void visit(arc::ast::Identifier* node) override;
    void visit(arc::ast::BinaryOp* node) override;
    void visit(arc::ast::UnaryOp* node) override;
    void visit(arc::ast::FunctionCall* node) override;
    void visit(arc::ast::MemberAccess* node) override;
    void visit(arc::ast::ArrayAccess* node) override;
    void visit(arc::ast::VariableDecl* node) override;
    void visit(arc::ast::ReturnStmt* node) override;
    void visit(arc::ast::ExpressionStmt* node) override;
    void visit(arc::ast::Block* node) override;
    void visit(arc::ast::IfStmt* node) override;
    void visit(arc::ast::ForStmt* node) override;
    void visit(arc::ast::WhileStmt* node) override;
    void visit(arc::ast::StructDecl* node) override;
    void visit(arc::ast::FunctionDecl* node) override;
    void visit(arc::ast::Program* node) override;
};

} // namespace arc::codegen

