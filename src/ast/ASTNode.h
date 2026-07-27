#pragma once

#include <memory>
#include <string>
#include <vector>
#include <variant>

namespace arc::ast {

class ASTVisitor;

class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual void accept(ASTVisitor* visitor) = 0;
};

enum class PrimitiveType {
    INT,
    FLOAT,
    DOUBLE,
    BOOL,
    CHAR,
    VOID,
};

struct Type {
    PrimitiveType base;
    int pointerLevel = 0;
    std::string structName;

    bool isVoid() const { return base == PrimitiveType::VOID && pointerLevel == 0; }
    bool isStruct() const { return !structName.empty(); }
};

class Expression : public ASTNode {
public:
    virtual ~Expression() = default;
};

class Statement : public ASTNode {
public:
    virtual ~Statement() = default;
};

class Literal : public Expression {
public:
    enum class Kind {
        INTEGER,
        FLOAT,
        STRING,
        BOOLEAN,
    };

    Kind kind;
    std::string value;

    Literal(Kind kind, const std::string& value)
        : kind(kind), value(value) {}

    void accept(ASTVisitor* visitor) override;
};

class Identifier : public Expression {
public:
    std::string name;

    explicit Identifier(const std::string& name)
        : name(name) {}

    void accept(ASTVisitor* visitor) override;
};

class BinaryOp : public Expression {
public:
    enum class Operator {
        PLUS,
        MINUS,
        MULTIPLY,
        DIVIDE,
        MODULO,
        EQUAL,
        NOT_EQUAL,
        LESS,
        LESS_EQUAL,
        GREATER,
        GREATER_EQUAL,
        LOGICAL_AND,
        LOGICAL_OR,
        BITWISE_AND,
        BITWISE_OR,
        BITWISE_XOR,
        ASSIGN,
    };

    Operator op;
    std::unique_ptr<Expression> left;
    std::unique_ptr<Expression> right;

    BinaryOp(Operator op, std::unique_ptr<Expression> left, std::unique_ptr<Expression> right)
        : op(op), left(std::move(left)), right(std::move(right)) {}

    void accept(ASTVisitor* visitor) override;
};

class UnaryOp : public Expression {
public:
    enum class Operator {
        NEGATE,
        NOT,
        BITWISE_NOT,
        DEREF,
        ADDRESS_OF,
    };

    Operator op;
    std::unique_ptr<Expression> operand;

    UnaryOp(Operator op, std::unique_ptr<Expression> operand)
        : op(op), operand(std::move(operand)) {}

    void accept(ASTVisitor* visitor) override;
};

class MemberAccess : public Expression {
public:
    std::unique_ptr<Expression> object;
    std::string member;

    MemberAccess(std::unique_ptr<Expression> object, const std::string& member)
        : object(std::move(object)), member(member) {}

    void accept(ASTVisitor* visitor) override;
};

class ArrayAccess : public Expression {
public:
    std::unique_ptr<Expression> array;
    std::unique_ptr<Expression> index;

    ArrayAccess(std::unique_ptr<Expression> array, std::unique_ptr<Expression> index)
        : array(std::move(array)), index(std::move(index)) {}

    void accept(ASTVisitor* visitor) override;
};

class FunctionCall : public Expression {
public:
    std::string functionName;
    std::vector<std::unique_ptr<Expression>> arguments;

    FunctionCall(const std::string& functionName,
                 std::vector<std::unique_ptr<Expression>> arguments)
        : functionName(functionName), arguments(std::move(arguments)) {}

    void accept(ASTVisitor* visitor) override;
};

class VariableDecl : public Statement {
public:
    Type type;
    std::string name;
    std::unique_ptr<Expression> initializer;

    VariableDecl(const Type& type, const std::string& name,
                 std::unique_ptr<Expression> initializer = nullptr)
        : type(type), name(name), initializer(std::move(initializer)) {}

    void accept(ASTVisitor* visitor) override;
};

class ReturnStmt : public Statement {
public:
    std::unique_ptr<Expression> value;

    explicit ReturnStmt(std::unique_ptr<Expression> value = nullptr)
        : value(std::move(value)) {}

    void accept(ASTVisitor* visitor) override;
};

class ExpressionStmt : public Statement {
public:
    std::unique_ptr<Expression> expression;

    explicit ExpressionStmt(std::unique_ptr<Expression> expression)
        : expression(std::move(expression)) {}

    void accept(ASTVisitor* visitor) override;
};

class Block : public Statement {
public:
    std::vector<std::unique_ptr<Statement>> statements;

    explicit Block(std::vector<std::unique_ptr<Statement>> statements = {})
        : statements(std::move(statements)) {}

    void accept(ASTVisitor* visitor) override;
};

class IfStmt : public Statement {
public:
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Statement> thenBranch;
    std::unique_ptr<Statement> elseBranch;

    IfStmt(std::unique_ptr<Expression> condition,
           std::unique_ptr<Statement> thenBranch,
           std::unique_ptr<Statement> elseBranch = nullptr)
        : condition(std::move(condition)),
          thenBranch(std::move(thenBranch)),
          elseBranch(std::move(elseBranch)) {}

    void accept(ASTVisitor* visitor) override;
};

class ForStmt : public Statement {
public:
    std::unique_ptr<Statement> init;
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> increment;
    std::unique_ptr<Statement> body;

    ForStmt(std::unique_ptr<Statement> init,
            std::unique_ptr<Expression> condition,
            std::unique_ptr<Expression> increment,
            std::unique_ptr<Statement> body)
        : init(std::move(init)),
          condition(std::move(condition)),
          increment(std::move(increment)),
          body(std::move(body)) {}

    void accept(ASTVisitor* visitor) override;
};

class WhileStmt : public Statement {
public:
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Statement> body;

    WhileStmt(std::unique_ptr<Expression> condition,
              std::unique_ptr<Statement> body)
        : condition(std::move(condition)), body(std::move(body)) {}

    void accept(ASTVisitor* visitor) override;
};

class Parameter {
public:
    Type type;
    std::string name;

    Parameter(const Type& type, const std::string& name)
        : type(type), name(name) {}
};

class StructField {
public:
    Type type;
    std::string name;

    StructField(const Type& type, const std::string& name)
        : type(type), name(name) {}
};

class StructDecl : public ASTNode {
public:
    std::string name;
    std::vector<StructField> fields;

    StructDecl(const std::string& name, std::vector<StructField> fields)
        : name(name), fields(std::move(fields)) {}

    void accept(ASTVisitor* visitor) override;
};

class FunctionDecl : public ASTNode {
public:
    Type returnType;
    std::string name;
    std::vector<Parameter> parameters;
    std::unique_ptr<Block> body;

    FunctionDecl(const Type& returnType,
                 const std::string& name,
                 std::vector<Parameter> parameters,
                 std::unique_ptr<Block> body)
        : returnType(returnType),
          name(name),
          parameters(std::move(parameters)),
          body(std::move(body)) {}

    void accept(ASTVisitor* visitor) override;
};

class Program : public ASTNode {
public:
    std::vector<std::unique_ptr<StructDecl>> structs;
    std::vector<std::unique_ptr<FunctionDecl>> functions;

    Program(std::vector<std::unique_ptr<StructDecl>> structs = {},
            std::vector<std::unique_ptr<FunctionDecl>> functions = {})
        : structs(std::move(structs)), functions(std::move(functions)) {}

    void accept(ASTVisitor* visitor) override;
};

class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;

    virtual void visit(Literal* node) = 0;
    virtual void visit(Identifier* node) = 0;
    virtual void visit(BinaryOp* node) = 0;
    virtual void visit(UnaryOp* node) = 0;
    virtual void visit(FunctionCall* node) = 0;
    virtual void visit(MemberAccess* node) = 0;
    virtual void visit(ArrayAccess* node) = 0;
    virtual void visit(VariableDecl* node) = 0;
    virtual void visit(ReturnStmt* node) = 0;
    virtual void visit(ExpressionStmt* node) = 0;
    virtual void visit(Block* node) = 0;
    virtual void visit(IfStmt* node) = 0;
    virtual void visit(ForStmt* node) = 0;
    virtual void visit(WhileStmt* node) = 0;
    virtual void visit(StructDecl* node) = 0;
    virtual void visit(FunctionDecl* node) = 0;
    virtual void visit(Program* node) = 0;
};

} // namespace arc::ast

