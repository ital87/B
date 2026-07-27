#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "ast/ASTNode.h"
#include "codegen/CodeGenerator.h"
#include "common/Common.h"

namespace fs = std::filesystem;

std::string readFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw arc::CompilerException("Cannot open file: " + filepath);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " <source.arc>" << std::endl;
}

void printVersion() {
    std::cout << "Arc 1.0.0" << std::endl;
}

class ASTPrinter : public arc::ast::ASTVisitor {
public:
    void visit(arc::ast::Literal* node) override {
        std::cout << "Literal(" << node->value << ")";
    }

    void visit(arc::ast::Identifier* node) override {
        std::cout << "Identifier(" << node->name << ")";
    }

    void visit(arc::ast::BinaryOp* node) override {
        std::cout << "BinaryOp(";
        node->left->accept(this);
        std::cout << ", ";
        node->right->accept(this);
        std::cout << ")";
    }

    void visit(arc::ast::UnaryOp* node) override {
        std::cout << "UnaryOp(";
        node->operand->accept(this);
        std::cout << ")";
    }

    void visit(arc::ast::FunctionCall* node) override {
        std::cout << "Call(" << node->functionName << ")";
    }

    void visit(arc::ast::MemberAccess* node) override {
        std::cout << "MemberAccess(." << node->member << ")";
    }

    void visit(arc::ast::ArrayAccess* node) override {
        std::cout << "ArrayAccess([...])";
    }

    void visit(arc::ast::VariableDecl* node) override {
        std::cout << "VarDecl(" << node->name << ")";
    }

    void visit(arc::ast::ReturnStmt* node) override {
        (void)node;
        std::cout << "Return";
    }

    void visit(arc::ast::ExpressionStmt* node) override {
        (void)node;
        std::cout << "ExprStmt";
    }

    void visit(arc::ast::Block* node) override {
        std::cout << "Block(" << node->statements.size() << " stmts)";
    }

    void visit(arc::ast::IfStmt* node) override {
        (void)node;
        std::cout << "If";
    }

    void visit(arc::ast::ForStmt* node) override {
        (void)node;
        std::cout << "For";
    }

    void visit(arc::ast::WhileStmt* node) override {
        (void)node;
        std::cout << "While";
    }

    void visit(arc::ast::StructDecl* node) override {
        std::cout << "Struct(" << node->name << ")";
    }

    void visit(arc::ast::FunctionDecl* node) override {
        std::cout << "Function(" << node->name << ")";
    }

    void visit(arc::ast::Program* node) override {
        std::cout << "Program(" << node->functions.size() << " functions)" << std::endl;
        for (const auto& func : node->functions) {
            func->accept(this);
            std::cout << std::endl;
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string firstArg = argv[1];
    if (firstArg == "--version" || firstArg == "-version" || firstArg == "-v") {
        printVersion();
        return 0;
    }

    std::string filepath = firstArg;
    bool debugMode = (argc > 2 && std::string(argv[2]) == "--debug");

    try {
        std::cout << "[1/4] Reading source file..." << std::endl;
        std::string source = readFile(filepath);

        std::cout << "[2/4] Lexing..." << std::endl;
        arc::lexer::Lexer lexer(source);
        auto tokens = lexer.tokenize();
        std::cout << "      Tokens: " << tokens.size() - 1 << std::endl;

        std::cout << "[3/4] Parsing..." << std::endl;
        arc::parser::Parser parser(tokens);
        auto program = parser.parse();
        std::cout << "      Functions: " << program->functions.size() << std::endl;

        if (debugMode) {
            std::cout << "      AST:" << std::endl;
            ASTPrinter printer;
            program->accept(&printer);
            std::cout << std::endl;
        }

        std::cout << "[4/4] Code generation..." << std::endl;

        arc::codegen::CodeGenerator codegen;

        std::string objectFile = fs::path(filepath).stem().string() + ".o";
        std::string executable = fs::path(filepath).stem().string();

        if (!codegen.generate(program.get(), objectFile)) {
            std::cerr << "Code generation failed" << std::endl;
            return 1;
        }
        std::cout << "      Object file: " << objectFile << std::endl;

        if (!codegen.linkExecutable(objectFile, executable)) {
            std::cerr << "Linking failed" << std::endl;
            return 1;
        }
        std::cout << "      Executable: " << executable << std::endl;

        std::cout << "\n=== Compilation successful ===" << std::endl;
        std::cout << "Run with: ./" << executable << std::endl;

        return 0;

    } catch (const arc::CompilerException& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

