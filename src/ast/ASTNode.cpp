#include "ASTNode.h"

namespace arc::ast {

void Literal::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

void Identifier::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

void BinaryOp::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

void UnaryOp::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

void FunctionCall::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

void MemberAccess::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

void ArrayAccess::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

void VariableDecl::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

void ReturnStmt::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

void ExpressionStmt::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

void Block::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

void IfStmt::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

void ForStmt::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

void WhileStmt::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

void StructDecl::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

void FunctionDecl::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

void Program::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

} // namespace arc::ast

