#include <stdio.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <string>
#include <cctype>
#include <stack>
#include <cstdlib>
#include <stdexcept>
#include <variant>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Triple.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Constants.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>

namespace fs = std::filesystem;

namespace b {

class CompilerException : public std::runtime_error {
public:
    explicit CompilerException(const std::string& message)
        : std::runtime_error(message) {
    }
};

}

namespace b::lexer {

enum class TokenType {
    INTEGER,
    FLOAT,
    STRING,
    CHAR_LITERAL,
    IDENTIFIER,

    KW_INT,
    KW_FLOAT,
    KW_DOUBLE,
    KW_BOOL,
    KW_CHAR,
    KW_STRING,
    KW_VOID,
    KW_RETURN,
    KW_IF,
    KW_ELSE,
    KW_FOR,
    KW_WHILE,
    KW_BREAK,
    KW_CONTINUE,
    KW_STRUCT,
    KW_ENUM,
    KW_TYPEDEF,
    KW_FUNCTION,
    KW_TRUE,
    KW_FALSE,
    KW_IMPORT,
    KW_SWITCH,
    KW_CASE,
    KW_DEFAULT,
    KW_CONST,

    PLUS,
    MINUS,
    STAR,
    SLASH,
    PERCENT,
    EQUAL,
    EQUAL_EQUAL,
    NOT_EQUAL,
    LESS,
    LESS_EQUAL,
    GREATER,
    GREATER_EQUAL,
    AMPERSAND,
    PIPE,
    CARET,
    TILDE,
    AND_AND,
    PIPE_PIPE,
    BANG,

    LPAREN,
    RPAREN,
    LBRACE,
    RBRACE,
    LBRACKET,
    RBRACKET,
    SEMICOLON,
    COLON,
    COMMA,
    DOT,
    ARROW,

    EOF_TOKEN,
    UNKNOWN,
};

struct Token {
    TokenType type;
    std::string lexeme;
    std::string value;
    int line;
    int column;

    Token();
    Token(TokenType type, const std::string& lexeme, const std::string& value, int line, int column);

    std::string typeToString() const;
};

std::ostream& operator<<(std::ostream& os, const Token& token);

Token::Token()
    : type(TokenType::UNKNOWN), lexeme(""), value(""), line(0), column(0) {
}

Token::Token(TokenType type, const std::string& lexeme, const std::string& value, int line, int column)
    : type(type), lexeme(lexeme), value(value), line(line), column(column) {
}

std::string Token::typeToString() const {
    switch (type) {
        case TokenType::INTEGER:        return "INTEGER";
        case TokenType::FLOAT:          return "FLOAT";
        case TokenType::STRING:         return "STRING";
        case TokenType::CHAR_LITERAL:   return "CHAR_LITERAL";
        case TokenType::IDENTIFIER:     return "IDENTIFIER";

        case TokenType::KW_INT:         return "KW_INT";
        case TokenType::KW_FLOAT:       return "KW_FLOAT";
        case TokenType::KW_DOUBLE:      return "KW_DOUBLE";
        case TokenType::KW_BOOL:        return "KW_BOOL";
        case TokenType::KW_CHAR:        return "KW_CHAR";
        case TokenType::KW_STRING:      return "KW_STRING";
        case TokenType::KW_VOID:        return "KW_VOID";
        case TokenType::KW_RETURN:      return "KW_RETURN";
        case TokenType::KW_IF:          return "KW_IF";
        case TokenType::KW_ELSE:        return "KW_ELSE";
        case TokenType::KW_FOR:         return "KW_FOR";
        case TokenType::KW_WHILE:       return "KW_WHILE";
        case TokenType::KW_STRUCT:      return "KW_STRUCT";
        case TokenType::KW_ENUM:        return "KW_ENUM";
        case TokenType::KW_FUNCTION:    return "KW_FUNCTION";
        case TokenType::KW_TRUE:        return "KW_TRUE";
        case TokenType::KW_FALSE:       return "KW_FALSE";
        case TokenType::KW_IMPORT:      return "KW_IMPORT";
        case TokenType::KW_SWITCH:      return "KW_SWITCH";
        case TokenType::KW_CASE:        return "KW_CASE";
        case TokenType::KW_DEFAULT:     return "KW_DEFAULT";
        case TokenType::KW_CONST:       return "KW_CONST";

        case TokenType::PLUS:           return "PLUS";
        case TokenType::MINUS:          return "MINUS";
        case TokenType::STAR:           return "STAR";
        case TokenType::SLASH:          return "SLASH";
        case TokenType::PERCENT:        return "PERCENT";
        case TokenType::EQUAL:          return "EQUAL";
        case TokenType::EQUAL_EQUAL:    return "EQUAL_EQUAL";
        case TokenType::NOT_EQUAL:      return "NOT_EQUAL";
        case TokenType::LESS:           return "LESS";
        case TokenType::LESS_EQUAL:     return "LESS_EQUAL";
        case TokenType::GREATER:        return "GREATER";
        case TokenType::GREATER_EQUAL:  return "GREATER_EQUAL";
        case TokenType::AMPERSAND:      return "AMPERSAND";
        case TokenType::PIPE:           return "PIPE";
        case TokenType::CARET:          return "CARET";
        case TokenType::TILDE:          return "TILDE";
        case TokenType::AND_AND:        return "AND_AND";
        case TokenType::PIPE_PIPE:      return "PIPE_PIPE";
        case TokenType::BANG:           return "BANG";

        case TokenType::LPAREN:         return "LPAREN";
        case TokenType::RPAREN:         return "RPAREN";
        case TokenType::LBRACE:         return "LBRACE";
        case TokenType::RBRACE:         return "RBRACE";
        case TokenType::LBRACKET:       return "LBRACKET";
        case TokenType::RBRACKET:       return "RBRACKET";
        case TokenType::SEMICOLON:      return "SEMICOLON";
        case TokenType::COLON:          return "COLON";
        case TokenType::COMMA:          return "COMMA";
        case TokenType::DOT:            return "DOT";
        case TokenType::ARROW:          return "ARROW";

        case TokenType::EOF_TOKEN:      return "EOF";
        case TokenType::UNKNOWN:        return "UNKNOWN";
        default:                        return "UNKNOWN";
    }
}

std::ostream& operator<<(std::ostream& os, const Token& token) {
    os << "[" << token.typeToString() << " | \"" << token.lexeme << "\"";
    if (!token.value.empty()) {
        os << " | value: " << token.value;
    }
    os << " @ L" << token.line << ":C" << token.column << "]";
    return os;
}

class Lexer {
public:
    explicit Lexer(const std::string& source);

    std::vector<Token> tokenize();

private:
    std::string source;
    size_t current;
    int line;
    int column;

    char peek() const;
    char peekNext() const;
    char advance();
    void skipWhitespace();
    void skipLineComment();
    void skipBlockComment();
    bool isAtEnd() const;

    Token readString(char quote);
    Token readNumber();
    Token readIdentifierOrKeyword();
    Token makeToken(TokenType type, const std::string& lexeme, const std::string& value = "");

    static const std::unordered_map<std::string, TokenType> keywords;
};

const std::unordered_map<std::string, TokenType> Lexer::keywords = {
    {"int", TokenType::KW_INT},
    {"float", TokenType::KW_FLOAT},
    {"double", TokenType::KW_DOUBLE},
    {"bool", TokenType::KW_BOOL},
    {"char", TokenType::KW_CHAR},
    {"string", TokenType::KW_STRING},
    {"void", TokenType::KW_VOID},
    {"return", TokenType::KW_RETURN},
    {"if", TokenType::KW_IF},
    {"else", TokenType::KW_ELSE},
    {"for", TokenType::KW_FOR},
    {"while", TokenType::KW_WHILE},
    {"break", TokenType::KW_BREAK},
    {"continue", TokenType::KW_CONTINUE},
    {"struct", TokenType::KW_STRUCT},
    {"enum", TokenType::KW_ENUM},
    {"typedef", TokenType::KW_TYPEDEF},
    {"fn", TokenType::KW_FUNCTION},
    {"true", TokenType::KW_TRUE},
    {"false", TokenType::KW_FALSE},
    {"import", TokenType::KW_IMPORT},
    {"switch", TokenType::KW_SWITCH},
    {"case", TokenType::KW_CASE},
    {"default", TokenType::KW_DEFAULT},
    {"const", TokenType::KW_CONST},
};

Lexer::Lexer(const std::string& source)
    : source(source), current(0), line(1), column(1) {
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (!isAtEnd()) {
        skipWhitespace();

        if (isAtEnd()) break;

        if (source[current] == '/' && peekNext() == '/') {
            skipLineComment();
            continue;
        }

        if (source[current] == '/' && peekNext() == '*') {
            skipBlockComment();
            continue;
        }

        char c = advance();

        switch (c) {
            case '(': tokens.push_back(makeToken(TokenType::LPAREN, "(")); break;
            case ')': tokens.push_back(makeToken(TokenType::RPAREN, ")")); break;
            case '{': tokens.push_back(makeToken(TokenType::LBRACE, "{")); break;
            case '}': tokens.push_back(makeToken(TokenType::RBRACE, "}")); break;
            case '[': tokens.push_back(makeToken(TokenType::LBRACKET, "[")); break;
            case ']': tokens.push_back(makeToken(TokenType::RBRACKET, "]")); break;
            case ';': tokens.push_back(makeToken(TokenType::SEMICOLON, ";")); break;
            case ':': tokens.push_back(makeToken(TokenType::COLON, ":")); break;
            case ',': tokens.push_back(makeToken(TokenType::COMMA, ",")); break;
            case '.': tokens.push_back(makeToken(TokenType::DOT, ".")); break;
            case '~': tokens.push_back(makeToken(TokenType::TILDE, "~")); break;

            case '+':
                tokens.push_back(makeToken(TokenType::PLUS, "+"));
                break;
            case '-':
                if (peek() == '>') {
                    advance();
                    tokens.push_back(makeToken(TokenType::ARROW, "->"));
                } else {
                    tokens.push_back(makeToken(TokenType::MINUS, "-"));
                }
                break;
            case '*':
                tokens.push_back(makeToken(TokenType::STAR, "*"));
                break;
            case '/':
                tokens.push_back(makeToken(TokenType::SLASH, "/"));
                break;
            case '%':
                tokens.push_back(makeToken(TokenType::PERCENT, "%"));
                break;

            case '=':
                if (peek() == '=') {
                    advance();
                    tokens.push_back(makeToken(TokenType::EQUAL_EQUAL, "=="));
                } else {
                    tokens.push_back(makeToken(TokenType::EQUAL, "="));
                }
                break;

            case '!':
                if (peek() == '=') {
                    advance();
                    tokens.push_back(makeToken(TokenType::NOT_EQUAL, "!="));
                } else {
                    tokens.push_back(makeToken(TokenType::BANG, "!"));
                }
                break;

            case '<':
                if (peek() == '=') {
                    advance();
                    tokens.push_back(makeToken(TokenType::LESS_EQUAL, "<="));
                } else {
                    tokens.push_back(makeToken(TokenType::LESS, "<"));
                }
                break;

            case '>':
                if (peek() == '=') {
                    advance();
                    tokens.push_back(makeToken(TokenType::GREATER_EQUAL, ">="));
                } else {
                    tokens.push_back(makeToken(TokenType::GREATER, ">"));
                }
                break;

            case '&':
                if (peek() == '&') {
                    advance();
                    tokens.push_back(makeToken(TokenType::AND_AND, "&&"));
                } else {
                    tokens.push_back(makeToken(TokenType::AMPERSAND, "&"));
                }
                break;

            case '|':
                if (peek() == '|') {
                    advance();
                    tokens.push_back(makeToken(TokenType::PIPE_PIPE, "||"));
                } else {
                    tokens.push_back(makeToken(TokenType::PIPE, "|"));
                }
                break;

            case '^':
                tokens.push_back(makeToken(TokenType::CARET, "^"));
                break;

            case '"':
            case '\'':
                tokens.push_back(readString(c));
                break;

            default:
                if (std::isdigit(c)) {
                    current--;
                    column--;
                    tokens.push_back(readNumber());
                } else if (std::isalpha(c) || c == '_') {
                    current--;
                    column--;
                    tokens.push_back(readIdentifierOrKeyword());
                } else {
                    tokens.push_back(makeToken(TokenType::UNKNOWN, std::string(1, c)));
                }
                break;
        }
    }

    tokens.push_back(Token(TokenType::EOF_TOKEN, "", "", line, column));
    return tokens;
}

char Lexer::peek() const {
    if (isAtEnd()) return '\0';
    return source[current];
}

char Lexer::peekNext() const {
    if (current + 1 >= source.size()) return '\0';
    return source[current + 1];
}

char Lexer::advance() {
    if (isAtEnd()) return '\0';

    char c = source[current++];
    if (c == '\n') {
        line++;
        column = 1;
    } else {
        column++;
    }
    return c;
}

void Lexer::skipWhitespace() {
    while (!isAtEnd()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else {
            break;
        }
    }
}

void Lexer::skipLineComment() {
    while (!isAtEnd() && peek() != '\n') {
        advance();
    }
    if (!isAtEnd()) advance();
}

void Lexer::skipBlockComment() {
    advance();
    advance();

    while (!isAtEnd()) {
        if (peek() == '*' && peekNext() == '/') {
            advance();
            advance();
            break;
        }
        advance();
    }
}

bool Lexer::isAtEnd() const {
    return current >= source.size();
}

Token Lexer::readString(char quote) {
    int startLine = line;
    int startColumn = column - 1;
    std::string value;

    while (!isAtEnd() && peek() != quote) {
        if (peek() == '\\') {
            advance();
            if (!isAtEnd()) {
                char escaped = advance();
                switch (escaped) {
                    case 'n': value += '\n'; break;
                    case 't': value += '\t'; break;
                    case 'r': value += '\r'; break;
                    case '0': value += '\0'; break;
                    case '\\': value += '\\'; break;
                    case '"': value += '"'; break;
                    case '\'': value += '\''; break;
                    default: value += escaped; break;
                }
            }
        } else {
            value += advance();
        }
    }

    if (isAtEnd()) {
        throw std::runtime_error("Unterminated string at line " + std::to_string(startLine));
    }

    advance();

    if (quote == '\'') {
        if (value.size() != 1) {
            throw std::runtime_error("Invalid character literal at line " + std::to_string(startLine));
        }
        int code = static_cast<int>(static_cast<unsigned char>(value[0]));
        return Token(TokenType::CHAR_LITERAL, "'" + value + "'", std::to_string(code), startLine, startColumn);
    }

    return Token(TokenType::STRING, "\"" + value + "\"", value, startLine, startColumn);
}

Token Lexer::readNumber() {
    int startLine = line;
    int startColumn = column;
    std::string lexeme;
    bool isFloat = false;

    while (!isAtEnd() && (std::isdigit(peek()) || peek() == '.')) {
        if (peek() == '.') {
            if (isFloat || peekNext() == '.') break;
            isFloat = true;
        }
        lexeme += advance();
    }

    TokenType type = isFloat ? TokenType::FLOAT : TokenType::INTEGER;
    return Token(type, lexeme, lexeme, startLine, startColumn);
}

Token Lexer::readIdentifierOrKeyword() {
    int startLine = line;
    int startColumn = column;
    std::string lexeme;

    while (!isAtEnd() && (std::isalnum(peek()) || peek() == '_')) {
        lexeme += advance();
    }

    auto it = keywords.find(lexeme);
    TokenType type = (it != keywords.end()) ? it->second : TokenType::IDENTIFIER;

    return Token(type, lexeme, lexeme, startLine, startColumn);
}

Token Lexer::makeToken(TokenType type, const std::string& lexeme, const std::string& value) {
    std::string finalValue = value.empty() ? lexeme : value;
    return Token(type, lexeme, finalValue, line, column - static_cast<int>(lexeme.size()));
}

}

namespace b::ast {

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
    std::string funcPointerTypedefName;

    bool isVoid() const { return base == PrimitiveType::VOID && pointerLevel == 0; }
    bool isStruct() const { return !structName.empty(); }
    bool isFunctionPointer() const { return !funcPointerTypedefName.empty(); }
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

class CastExpr : public Expression {
public:
    Type targetType;
    std::unique_ptr<Expression> expr;

    CastExpr(const Type& targetType, std::unique_ptr<Expression> expr)
        : targetType(targetType), expr(std::move(expr)) {}

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

class SwitchCase {
public:
    std::unique_ptr<Expression> value;
    std::vector<std::unique_ptr<Statement>> statements;
    bool isDefault;

    SwitchCase(std::unique_ptr<Expression> value,
               std::vector<std::unique_ptr<Statement>> statements,
               bool isDefault = false)
        : value(std::move(value)), statements(std::move(statements)), isDefault(isDefault) {}
};

class SwitchStmt : public Statement {
public:
    std::unique_ptr<Expression> condition;
    std::vector<SwitchCase> cases;

    SwitchStmt(std::unique_ptr<Expression> condition,
               std::vector<SwitchCase> cases)
        : condition(std::move(condition)), cases(std::move(cases)) {}

    void accept(ASTVisitor* visitor) override;
};

class VariableDecl : public Statement {
public:
    Type type;
    std::string name;
    std::unique_ptr<Expression> initializer;
    bool isConst;

    VariableDecl(const Type& type, const std::string& name,
                 std::unique_ptr<Expression> initializer = nullptr,
                 bool isConst = false)
        : type(type), name(name), initializer(std::move(initializer)), isConst(isConst) {}

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

class BreakStmt : public Statement {
public:
    void accept(ASTVisitor* visitor) override;
};

class ContinueStmt : public Statement {
public:
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

struct FuncPointerTypedef {
    std::string name;
    Type returnType;
    std::vector<Type> paramTypes;
};

struct GlobalVariable {
    Type type;
    std::string name;
    std::unique_ptr<Expression> initializer;
    bool isConst;

    GlobalVariable(const Type& type, const std::string& name,
                   std::unique_ptr<Expression> initializer,
                   bool isConst = false)
        : type(type), name(name), initializer(std::move(initializer)), isConst(isConst) {}
};

class Program : public ASTNode {
public:
    std::vector<std::unique_ptr<StructDecl>> structs;
    std::vector<std::unique_ptr<FunctionDecl>> functions;
    std::vector<FuncPointerTypedef> funcPointerTypedefs;
    std::vector<GlobalVariable> globalVariables;

    Program(std::vector<std::unique_ptr<StructDecl>> structs = {},
            std::vector<std::unique_ptr<FunctionDecl>> functions = {},
            std::vector<FuncPointerTypedef> funcPointerTypedefs = {},
            std::vector<GlobalVariable> globalVariables = {})
        : structs(std::move(structs)), functions(std::move(functions)),
          funcPointerTypedefs(std::move(funcPointerTypedefs)),
          globalVariables(std::move(globalVariables)) {}

    void accept(ASTVisitor* visitor) override;
};

class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;

    virtual void visit(Literal* node) = 0;
    virtual void visit(Identifier* node) = 0;
    virtual void visit(BinaryOp* node) = 0;
    virtual void visit(UnaryOp* node) = 0;
    virtual void visit(CastExpr* node) = 0;
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
    virtual void visit(BreakStmt* node) = 0;
    virtual void visit(ContinueStmt* node) = 0;
    virtual void visit(SwitchStmt* node) = 0;
    virtual void visit(StructDecl* node) = 0;
    virtual void visit(FunctionDecl* node) = 0;
    virtual void visit(Program* node) = 0;
};

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

void CastExpr::accept(ASTVisitor* visitor) {
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

void BreakStmt::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

void ContinueStmt::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

void SwitchStmt::accept(ASTVisitor* visitor) {
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

}

namespace b::parser {

class ParseException : public std::runtime_error {
public:
    explicit ParseException(const std::string& message)
        : std::runtime_error("Parse Error: " + message) {}
};

class Parser {
public:
    explicit Parser(const std::vector<b::lexer::Token>& tokens);

    std::unique_ptr<b::ast::Program> parse();

private:
    std::vector<b::lexer::Token> tokens;
    size_t current;
    std::unordered_map<std::string, int> enumConstants;
    std::unordered_set<std::string> enumTypeNames;
    std::unordered_set<std::string> funcPointerTypedefNames;
    std::unordered_set<std::string> visitedFiles;
    std::unordered_set<std::string> constVariables;

    b::lexer::Token peek() const;
    b::lexer::Token previous() const;
    b::lexer::Token advance();
    bool check(b::lexer::TokenType type) const;
    bool match(b::lexer::TokenType type);
    bool match(const std::vector<b::lexer::TokenType>& types);
    b::lexer::Token consume(b::lexer::TokenType type, const std::string& message);
    bool isAtEnd() const;

    std::unique_ptr<b::ast::StructDecl> parseStructDecl();
    void parseEnumDecl();
    b::ast::FuncPointerTypedef parseTypedefDecl();
    std::unique_ptr<b::ast::FunctionDecl> parseFunctionDecl();
    std::unique_ptr<b::ast::Statement> parseStatement();
    std::unique_ptr<b::ast::Statement> parseBlock();
    std::unique_ptr<b::ast::Statement> parseVariableDecl();
    std::unique_ptr<b::ast::Statement> parseIfStmt();
    std::unique_ptr<b::ast::Statement> parseForStmt();
    std::unique_ptr<b::ast::Statement> parseWhileStmt();
    std::unique_ptr<b::ast::Statement> parseReturnStmt();
    std::unique_ptr<b::ast::Statement> parseSwitchStmt();
    std::unique_ptr<b::ast::Statement> parseExpressionStmt();

    std::unique_ptr<b::ast::Expression> parseExpression();
    std::unique_ptr<b::ast::Expression> parseAssignment();
    std::unique_ptr<b::ast::Expression> parseLogicalOr();
    std::unique_ptr<b::ast::Expression> parseLogicalAnd();
    std::unique_ptr<b::ast::Expression> parseBitwiseOr();
    std::unique_ptr<b::ast::Expression> parseBitwiseXor();
    std::unique_ptr<b::ast::Expression> parseBitwiseAnd();
    std::unique_ptr<b::ast::Expression> parseEquality();
    std::unique_ptr<b::ast::Expression> parseComparison();
    std::unique_ptr<b::ast::Expression> parseAdditive();
    std::unique_ptr<b::ast::Expression> parseMultiplicative();
    std::unique_ptr<b::ast::Expression> parseUnary();
    std::unique_ptr<b::ast::Expression> parsePostfix();
    std::unique_ptr<b::ast::Expression> parsePrimary();

    std::vector<b::ast::Parameter> parseParameterList();
    b::ast::Type parseType();
};

Parser::Parser(const std::vector<b::lexer::Token>& tokens)
    : tokens(tokens), current(0) {}

std::unique_ptr<b::ast::Program> Parser::parse() {
    std::vector<std::unique_ptr<b::ast::StructDecl>> structs;
    std::vector<std::unique_ptr<b::ast::FunctionDecl>> functions;
    std::vector<b::ast::FuncPointerTypedef> typedefs;
    std::vector<b::ast::GlobalVariable> globals;

    while (!isAtEnd()) {
        if (check(b::lexer::TokenType::EOF_TOKEN)) break;
        if (check(b::lexer::TokenType::KW_IMPORT)) {
            advance();
            if (check(b::lexer::TokenType::STRING)) {
                std::string path = peek().value;
                advance();
                consume(b::lexer::TokenType::SEMICOLON, "Expected ';' after import");
            }
        } else if (check(b::lexer::TokenType::KW_STRUCT)) {
            structs.push_back(parseStructDecl());
        } else if (check(b::lexer::TokenType::KW_ENUM)) {
            parseEnumDecl();
        } else if (check(b::lexer::TokenType::KW_TYPEDEF)) {
            typedefs.push_back(parseTypedefDecl());
        } else if (check(b::lexer::TokenType::KW_CONST)) {
            advance();
            b::ast::Type type = parseType();
            std::string name = consume(b::lexer::TokenType::IDENTIFIER, "Expected identifier").lexeme;
            consume(b::lexer::TokenType::EQUAL, "Expected '=' in global const declaration");
            auto init = parseExpression();
            consume(b::lexer::TokenType::SEMICOLON, "Expected ';' after global const");
            globals.emplace_back(type, name, std::move(init), true);
            constVariables.insert(name);
        } else if (check(b::lexer::TokenType::KW_INT) ||
                   check(b::lexer::TokenType::KW_FLOAT) ||
                   check(b::lexer::TokenType::KW_DOUBLE) ||
                   check(b::lexer::TokenType::KW_BOOL) ||
                   check(b::lexer::TokenType::KW_CHAR) ||
                   check(b::lexer::TokenType::KW_STRING)) {
            size_t saveTypePos = current;
            b::ast::Type type = parseType();
            if (check(b::lexer::TokenType::IDENTIFIER)) {
                std::string name = peek().lexeme;
                size_t saveNamePos = current;
                advance();
                if (check(b::lexer::TokenType::EQUAL) || check(b::lexer::TokenType::SEMICOLON)) {
                    std::unique_ptr<b::ast::Expression> init = nullptr;
                    if (check(b::lexer::TokenType::EQUAL)) {
                        advance();
                        init = parseExpression();
                    }
                    consume(b::lexer::TokenType::SEMICOLON, "Expected ';' after global variable");
                    globals.emplace_back(type, name, std::move(init), false);
                } else {
                    current = saveTypePos;
                    functions.push_back(parseFunctionDecl());
                }
            } else {
                throw b::CompilerException("Unexpected token in top-level");
            }
        } else {
            functions.push_back(parseFunctionDecl());
        }
    }

    return std::make_unique<b::ast::Program>(std::move(structs), std::move(functions), std::move(typedefs), std::move(globals));
}

b::lexer::Token Parser::peek() const {
    return tokens[current];
}

b::lexer::Token Parser::previous() const {
    return tokens[current - 1];
}

b::lexer::Token Parser::advance() {
    if (!isAtEnd()) current++;
    return previous();
}

bool Parser::check(b::lexer::TokenType type) const {
    if (isAtEnd()) return false;
    return peek().type == type;
}

bool Parser::match(b::lexer::TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::match(const std::vector<b::lexer::TokenType>& types) {
    for (auto type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

b::lexer::Token Parser::consume(b::lexer::TokenType type, const std::string& message) {
    if (check(type)) return advance();
    throw ParseException(message);
}

bool Parser::isAtEnd() const {
    return peek().type == b::lexer::TokenType::EOF_TOKEN;
}

b::ast::Type Parser::parseType() {
    b::ast::Type type;
    type.pointerLevel = 0;

    if (match(b::lexer::TokenType::KW_INT)) {
        type.base = b::ast::PrimitiveType::INT;
    } else if (match(b::lexer::TokenType::KW_FLOAT)) {
        type.base = b::ast::PrimitiveType::FLOAT;
    } else if (match(b::lexer::TokenType::KW_DOUBLE)) {
        type.base = b::ast::PrimitiveType::DOUBLE;
    } else if (match(b::lexer::TokenType::KW_BOOL)) {
        type.base = b::ast::PrimitiveType::BOOL;
    } else if (match(b::lexer::TokenType::KW_CHAR)) {
        type.base = b::ast::PrimitiveType::CHAR;
    } else if (match(b::lexer::TokenType::KW_STRING)) {
        type.base = b::ast::PrimitiveType::CHAR;
        type.pointerLevel = 1;
    } else if (match(b::lexer::TokenType::KW_VOID)) {
        type.base = b::ast::PrimitiveType::VOID;
    } else if (check(b::lexer::TokenType::IDENTIFIER) &&
               enumTypeNames.count(peek().lexeme) > 0) {
        advance();
        type.base = b::ast::PrimitiveType::INT;
    } else if (check(b::lexer::TokenType::IDENTIFIER) &&
               funcPointerTypedefNames.count(peek().lexeme) > 0) {
        type.funcPointerTypedefName = advance().lexeme;
        type.base = b::ast::PrimitiveType::INT;
    } else if (check(b::lexer::TokenType::IDENTIFIER)) {
        type.structName = advance().lexeme;
        type.base = b::ast::PrimitiveType::INT;
    } else {
        throw ParseException("Expected type, got " + peek().lexeme);
    }

    while (match(b::lexer::TokenType::STAR)) {
        type.pointerLevel++;
    }

    return type;
}

std::vector<b::ast::Parameter> Parser::parseParameterList() {
    std::vector<b::ast::Parameter> parameters;

    if (!check(b::lexer::TokenType::RPAREN)) {
        do {
            b::ast::Type type = parseType();
            std::string name = consume(b::lexer::TokenType::IDENTIFIER, "Expected parameter name").lexeme;
            parameters.emplace_back(type, name);
        } while (match(b::lexer::TokenType::COMMA));
    }

    return parameters;
}

std::unique_ptr<b::ast::StructDecl> Parser::parseStructDecl() {
    consume(b::lexer::TokenType::KW_STRUCT, "Expected 'struct'");
    std::string structName = consume(b::lexer::TokenType::IDENTIFIER, "Expected struct name").lexeme;
    consume(b::lexer::TokenType::LBRACE, "Expected '{' after struct name");

    std::vector<b::ast::StructField> fields;
    while (!check(b::lexer::TokenType::RBRACE) && !isAtEnd()) {
        b::ast::Type fieldType = parseType();
        std::string fieldName = consume(b::lexer::TokenType::IDENTIFIER, "Expected field name").lexeme;
        consume(b::lexer::TokenType::SEMICOLON, "Expected ';' after field");
        fields.emplace_back(fieldType, fieldName);
    }

    consume(b::lexer::TokenType::RBRACE, "Expected '}' after struct body");
    consume(b::lexer::TokenType::SEMICOLON, "Expected ';' after struct declaration");

    return std::make_unique<b::ast::StructDecl>(structName, std::move(fields));
}

void Parser::parseEnumDecl() {
    consume(b::lexer::TokenType::KW_ENUM, "Expected 'enum'");
    std::string enumName = consume(b::lexer::TokenType::IDENTIFIER, "Expected enum name").lexeme;
    consume(b::lexer::TokenType::LBRACE, "Expected '{' after enum name");

    enumTypeNames.insert(enumName);
    int nextValue = 0;

    while (!check(b::lexer::TokenType::RBRACE) && !isAtEnd()) {
        std::string valueName = consume(b::lexer::TokenType::IDENTIFIER, "Expected enum value name").lexeme;

        int value = nextValue;
        if (match(b::lexer::TokenType::EQUAL)) {
            b::lexer::Token numberToken = consume(b::lexer::TokenType::INTEGER, "Expected integer after '=' in enum");
            value = std::stoi(numberToken.value);
        }

        enumConstants[valueName] = value;
        nextValue = value + 1;

        if (!match(b::lexer::TokenType::COMMA)) {
            break;
        }
    }

    consume(b::lexer::TokenType::RBRACE, "Expected '}' after enum body");
    consume(b::lexer::TokenType::SEMICOLON, "Expected ';' after enum declaration");
}

b::ast::FuncPointerTypedef Parser::parseTypedefDecl() {
    consume(b::lexer::TokenType::KW_TYPEDEF, "Expected 'typedef'");
    b::ast::Type returnType = parseType();

    consume(b::lexer::TokenType::LPAREN, "Expected '(' in function pointer typedef");
    consume(b::lexer::TokenType::STAR, "Expected '*' in function pointer typedef");
    std::string typedefName = consume(b::lexer::TokenType::IDENTIFIER, "Expected typedef name").lexeme;
    consume(b::lexer::TokenType::RPAREN, "Expected ')' after typedef name");

    consume(b::lexer::TokenType::LPAREN, "Expected '(' before parameter types");
    std::vector<b::ast::Type> paramTypes;
    if (!check(b::lexer::TokenType::RPAREN)) {
        do {
            paramTypes.push_back(parseType());
        } while (match(b::lexer::TokenType::COMMA));
    }
    consume(b::lexer::TokenType::RPAREN, "Expected ')' after parameter types");
    consume(b::lexer::TokenType::SEMICOLON, "Expected ';' after typedef declaration");

    funcPointerTypedefNames.insert(typedefName);

    b::ast::FuncPointerTypedef result;
    result.name = typedefName;
    result.returnType = returnType;
    result.paramTypes = paramTypes;
    return result;
}

std::unique_ptr<b::ast::FunctionDecl> Parser::parseFunctionDecl() {
    b::ast::Type returnType = parseType();
    std::string functionName = consume(b::lexer::TokenType::IDENTIFIER, "Expected function name").lexeme;

    consume(b::lexer::TokenType::LPAREN, "Expected '(' after function name");
    auto parameters = parseParameterList();
    consume(b::lexer::TokenType::RPAREN, "Expected ')' after parameters");

    consume(b::lexer::TokenType::LBRACE, "Expected '{' before function body");
    auto body = std::unique_ptr<b::ast::Block>(
        dynamic_cast<b::ast::Block*>(parseBlock().release())
    );
    consume(b::lexer::TokenType::RBRACE, "Expected '}' after function body");

    return std::make_unique<b::ast::FunctionDecl>(
        returnType, functionName, std::move(parameters), std::move(body)
    );
}

std::unique_ptr<b::ast::Statement> Parser::parseStatement() {
    if (match(b::lexer::TokenType::LBRACE)) {
        auto block = parseBlock();
        consume(b::lexer::TokenType::RBRACE, "Expected '}' after block");
        return block;
    }
    if (check(b::lexer::TokenType::KW_INT) ||
        check(b::lexer::TokenType::KW_FLOAT) ||
        check(b::lexer::TokenType::KW_DOUBLE) ||
        check(b::lexer::TokenType::KW_BOOL) ||
        check(b::lexer::TokenType::KW_CHAR) ||
        check(b::lexer::TokenType::KW_STRING) ||
        check(b::lexer::TokenType::KW_VOID)) {
        return parseVariableDecl();
    }
    if (check(b::lexer::TokenType::KW_CONST)) {
        advance();
        b::ast::Type type = parseType();
        std::string name = consume(b::lexer::TokenType::IDENTIFIER, "Expected identifier").lexeme;
        consume(b::lexer::TokenType::EQUAL, "Expected '=' in const declaration");
        auto init = parseExpression();
        consume(b::lexer::TokenType::SEMICOLON, "Expected ';' after const");
        constVariables.insert(name);
        return std::make_unique<b::ast::VariableDecl>(type, name, std::move(init), true);
    }
    if (check(b::lexer::TokenType::IDENTIFIER)) {
        size_t lookAhead = current + 1;
        while (lookAhead < tokens.size() &&
               (tokens[lookAhead].type == b::lexer::TokenType::STAR ||
                tokens[lookAhead].type == b::lexer::TokenType::AMPERSAND)) {
            lookAhead++;
        }
        if (lookAhead < tokens.size() &&
            tokens[lookAhead].type == b::lexer::TokenType::IDENTIFIER) {
            return parseVariableDecl();
        }
    }
    if (match(b::lexer::TokenType::KW_IF)) {
        return parseIfStmt();
    }
    if (match(b::lexer::TokenType::KW_SWITCH)) {
        return parseSwitchStmt();
    }
    if (match(b::lexer::TokenType::KW_FOR)) {
        return parseForStmt();
    }
    if (match(b::lexer::TokenType::KW_WHILE)) {
        return parseWhileStmt();
    }
    if (match(b::lexer::TokenType::KW_RETURN)) {
        return parseReturnStmt();
    }
    if (match(b::lexer::TokenType::KW_BREAK)) {
        consume(b::lexer::TokenType::SEMICOLON, "Expected ';' after 'break'");
        return std::make_unique<b::ast::BreakStmt>();
    }
    if (match(b::lexer::TokenType::KW_CONTINUE)) {
        consume(b::lexer::TokenType::SEMICOLON, "Expected ';' after 'continue'");
        return std::make_unique<b::ast::ContinueStmt>();
    }

    return parseExpressionStmt();
}

std::unique_ptr<b::ast::Statement> Parser::parseBlock() {
    std::vector<std::unique_ptr<b::ast::Statement>> statements;

    while (!check(b::lexer::TokenType::RBRACE) && !isAtEnd()) {
        statements.push_back(parseStatement());
    }

    return std::make_unique<b::ast::Block>(std::move(statements));
}

std::unique_ptr<b::ast::Statement> Parser::parseVariableDecl() {
    b::ast::Type type = parseType();
    std::string name = consume(b::lexer::TokenType::IDENTIFIER, "Expected variable name").lexeme;

    std::unique_ptr<b::ast::Expression> initializer = nullptr;
    if (match(b::lexer::TokenType::EQUAL)) {
        initializer = parseExpression();
    }

    consume(b::lexer::TokenType::SEMICOLON, "Expected ';' after variable declaration");
    return std::make_unique<b::ast::VariableDecl>(type, name, std::move(initializer));
}

std::unique_ptr<b::ast::Statement> Parser::parseIfStmt() {
    consume(b::lexer::TokenType::LPAREN, "Expected '(' after 'if'");
    auto condition = parseExpression();
    consume(b::lexer::TokenType::RPAREN, "Expected ')' after if condition");

    auto thenBranch = parseStatement();

    std::unique_ptr<b::ast::Statement> elseBranch = nullptr;
    if (match(b::lexer::TokenType::KW_ELSE)) {
        elseBranch = parseStatement();
    }

    return std::make_unique<b::ast::IfStmt>(
        std::move(condition), std::move(thenBranch), std::move(elseBranch)
    );
}

std::unique_ptr<b::ast::Statement> Parser::parseForStmt() {
    consume(b::lexer::TokenType::LPAREN, "Expected '(' after 'for'");

    std::unique_ptr<b::ast::Statement> init = nullptr;
    if (match(b::lexer::TokenType::SEMICOLON)) {
        init = nullptr;
    } else if (check(b::lexer::TokenType::KW_INT) ||
               check(b::lexer::TokenType::KW_FLOAT) ||
               check(b::lexer::TokenType::KW_DOUBLE) ||
               check(b::lexer::TokenType::KW_BOOL) ||
               check(b::lexer::TokenType::KW_CHAR) ||
               check(b::lexer::TokenType::KW_STRING)) {
        init = parseVariableDecl();
    } else {
        init = parseExpressionStmt();
    }

    std::unique_ptr<b::ast::Expression> condition = nullptr;
    if (!check(b::lexer::TokenType::SEMICOLON)) {
        condition = parseExpression();
    }
    consume(b::lexer::TokenType::SEMICOLON, "Expected ';' after for condition");

    std::unique_ptr<b::ast::Expression> increment = nullptr;
    if (!check(b::lexer::TokenType::RPAREN)) {
        increment = parseExpression();
    }
    consume(b::lexer::TokenType::RPAREN, "Expected ')' after for clauses");

    auto body = parseStatement();

    return std::make_unique<b::ast::ForStmt>(
        std::move(init), std::move(condition), std::move(increment), std::move(body)
    );
}

std::unique_ptr<b::ast::Statement> Parser::parseWhileStmt() {
    consume(b::lexer::TokenType::LPAREN, "Expected '(' after 'while'");
    auto condition = parseExpression();
    consume(b::lexer::TokenType::RPAREN, "Expected ')' after while condition");

    auto body = parseStatement();

    return std::make_unique<b::ast::WhileStmt>(std::move(condition), std::move(body));
}

std::unique_ptr<b::ast::Statement> Parser::parseSwitchStmt() {
    consume(b::lexer::TokenType::LPAREN, "Expected '(' after 'switch'");
    auto condition = parseExpression();
    consume(b::lexer::TokenType::RPAREN, "Expected ')' after switch condition");
    consume(b::lexer::TokenType::LBRACE, "Expected '{' after switch");

    std::vector<b::ast::SwitchCase> cases;

    while (!check(b::lexer::TokenType::RBRACE) && !isAtEnd()) {
        if (match(b::lexer::TokenType::KW_CASE)) {
            auto caseValue = parseExpression();
            consume(b::lexer::TokenType::COLON, "Expected ':' after case value");

            std::vector<std::unique_ptr<b::ast::Statement>> caseStmts;
            while (!check(b::lexer::TokenType::KW_CASE) &&
                   !check(b::lexer::TokenType::KW_DEFAULT) &&
                   !check(b::lexer::TokenType::RBRACE) &&
                   !isAtEnd()) {
                caseStmts.push_back(parseStatement());
            }

            cases.emplace_back(std::move(caseValue), std::move(caseStmts), false);
        } else if (match(b::lexer::TokenType::KW_DEFAULT)) {
            consume(b::lexer::TokenType::COLON, "Expected ':' after default");

            std::vector<std::unique_ptr<b::ast::Statement>> defaultStmts;
            while (!check(b::lexer::TokenType::KW_CASE) &&
                   !check(b::lexer::TokenType::RBRACE) &&
                   !isAtEnd()) {
                defaultStmts.push_back(parseStatement());
            }

            cases.emplace_back(nullptr, std::move(defaultStmts), true);
        } else {
            throw b::CompilerException("Expected 'case' or 'default' in switch");
        }
    }

    consume(b::lexer::TokenType::RBRACE, "Expected '}' after switch body");
    return std::make_unique<b::ast::SwitchStmt>(std::move(condition), std::move(cases));
}

std::unique_ptr<b::ast::Statement> Parser::parseReturnStmt() {
    std::unique_ptr<b::ast::Expression> value = nullptr;

    if (!check(b::lexer::TokenType::SEMICOLON)) {
        value = parseExpression();
    }

    consume(b::lexer::TokenType::SEMICOLON, "Expected ';' after return statement");
    return std::make_unique<b::ast::ReturnStmt>(std::move(value));
}

std::unique_ptr<b::ast::Statement> Parser::parseExpressionStmt() {
    auto expr = parseExpression();
    consume(b::lexer::TokenType::SEMICOLON, "Expected ';' after expression");
    return std::make_unique<b::ast::ExpressionStmt>(std::move(expr));
}

std::unique_ptr<b::ast::Expression> Parser::parseExpression() {
    return parseAssignment();
}

std::unique_ptr<b::ast::Expression> Parser::parseAssignment() {
    auto expr = parseLogicalOr();

    if (match(b::lexer::TokenType::EQUAL)) {
        auto right = parseAssignment();
        return std::make_unique<b::ast::BinaryOp>(
            b::ast::BinaryOp::Operator::ASSIGN,
            std::move(expr),
            std::move(right)
        );
    }

    return expr;
}

std::unique_ptr<b::ast::Expression> Parser::parseLogicalOr() {
    auto expr = parseLogicalAnd();

    while (match(b::lexer::TokenType::PIPE_PIPE)) {
        auto right = parseLogicalAnd();
        expr = std::make_unique<b::ast::BinaryOp>(
            b::ast::BinaryOp::Operator::LOGICAL_OR,
            std::move(expr),
            std::move(right)
        );
    }

    return expr;
}

std::unique_ptr<b::ast::Expression> Parser::parseLogicalAnd() {
    auto expr = parseBitwiseOr();

    while (match(b::lexer::TokenType::AND_AND)) {
        auto right = parseBitwiseOr();
        expr = std::make_unique<b::ast::BinaryOp>(
            b::ast::BinaryOp::Operator::LOGICAL_AND,
            std::move(expr),
            std::move(right)
        );
    }

    return expr;
}

std::unique_ptr<b::ast::Expression> Parser::parseBitwiseOr() {
    auto expr = parseBitwiseXor();

    while (match(b::lexer::TokenType::PIPE)) {
        auto right = parseBitwiseXor();
        expr = std::make_unique<b::ast::BinaryOp>(
            b::ast::BinaryOp::Operator::BITWISE_OR,
            std::move(expr),
            std::move(right)
        );
    }

    return expr;
}

std::unique_ptr<b::ast::Expression> Parser::parseBitwiseXor() {
    auto expr = parseBitwiseAnd();

    while (match(b::lexer::TokenType::CARET)) {
        auto right = parseBitwiseAnd();
        expr = std::make_unique<b::ast::BinaryOp>(
            b::ast::BinaryOp::Operator::BITWISE_XOR,
            std::move(expr),
            std::move(right)
        );
    }

    return expr;
}

std::unique_ptr<b::ast::Expression> Parser::parseBitwiseAnd() {
    auto expr = parseEquality();

    while (match(b::lexer::TokenType::AMPERSAND)) {
        auto right = parseEquality();
        expr = std::make_unique<b::ast::BinaryOp>(
            b::ast::BinaryOp::Operator::BITWISE_AND,
            std::move(expr),
            std::move(right)
        );
    }

    return expr;
}

std::unique_ptr<b::ast::Expression> Parser::parseEquality() {
    auto expr = parseComparison();

    while (true) {
        if (match(b::lexer::TokenType::EQUAL_EQUAL)) {
            auto right = parseComparison();
            expr = std::make_unique<b::ast::BinaryOp>(
                b::ast::BinaryOp::Operator::EQUAL,
                std::move(expr),
                std::move(right)
            );
        } else if (match(b::lexer::TokenType::NOT_EQUAL)) {
            auto right = parseComparison();
            expr = std::make_unique<b::ast::BinaryOp>(
                b::ast::BinaryOp::Operator::NOT_EQUAL,
                std::move(expr),
                std::move(right)
            );
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<b::ast::Expression> Parser::parseComparison() {
    auto expr = parseAdditive();

    while (true) {
        if (match(b::lexer::TokenType::LESS)) {
            auto right = parseAdditive();
            expr = std::make_unique<b::ast::BinaryOp>(
                b::ast::BinaryOp::Operator::LESS,
                std::move(expr),
                std::move(right)
            );
        } else if (match(b::lexer::TokenType::LESS_EQUAL)) {
            auto right = parseAdditive();
            expr = std::make_unique<b::ast::BinaryOp>(
                b::ast::BinaryOp::Operator::LESS_EQUAL,
                std::move(expr),
                std::move(right)
            );
        } else if (match(b::lexer::TokenType::GREATER)) {
            auto right = parseAdditive();
            expr = std::make_unique<b::ast::BinaryOp>(
                b::ast::BinaryOp::Operator::GREATER,
                std::move(expr),
                std::move(right)
            );
        } else if (match(b::lexer::TokenType::GREATER_EQUAL)) {
            auto right = parseAdditive();
            expr = std::make_unique<b::ast::BinaryOp>(
                b::ast::BinaryOp::Operator::GREATER_EQUAL,
                std::move(expr),
                std::move(right)
            );
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<b::ast::Expression> Parser::parseAdditive() {
    auto expr = parseMultiplicative();

    while (true) {
        if (match(b::lexer::TokenType::PLUS)) {
            auto right = parseMultiplicative();
            expr = std::make_unique<b::ast::BinaryOp>(
                b::ast::BinaryOp::Operator::PLUS,
                std::move(expr),
                std::move(right)
            );
        } else if (match(b::lexer::TokenType::MINUS)) {
            auto right = parseMultiplicative();
            expr = std::make_unique<b::ast::BinaryOp>(
                b::ast::BinaryOp::Operator::MINUS,
                std::move(expr),
                std::move(right)
            );
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<b::ast::Expression> Parser::parseMultiplicative() {
    auto expr = parseUnary();

    while (true) {
        if (match(b::lexer::TokenType::STAR)) {
            auto right = parseUnary();
            expr = std::make_unique<b::ast::BinaryOp>(
                b::ast::BinaryOp::Operator::MULTIPLY,
                std::move(expr),
                std::move(right)
            );
        } else if (match(b::lexer::TokenType::SLASH)) {
            auto right = parseUnary();
            expr = std::make_unique<b::ast::BinaryOp>(
                b::ast::BinaryOp::Operator::DIVIDE,
                std::move(expr),
                std::move(right)
            );
        } else if (match(b::lexer::TokenType::PERCENT)) {
            auto right = parseUnary();
            expr = std::make_unique<b::ast::BinaryOp>(
                b::ast::BinaryOp::Operator::MODULO,
                std::move(expr),
                std::move(right)
            );
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<b::ast::Expression> Parser::parseUnary() {
    if (check(b::lexer::TokenType::LPAREN)) {
        size_t checkpoint = current;
        advance();

        bool looksLikeType =
            check(b::lexer::TokenType::KW_INT) ||
            check(b::lexer::TokenType::KW_FLOAT) ||
            check(b::lexer::TokenType::KW_DOUBLE) ||
            check(b::lexer::TokenType::KW_BOOL) ||
            check(b::lexer::TokenType::KW_CHAR) ||
            check(b::lexer::TokenType::KW_STRING) ||
            check(b::lexer::TokenType::KW_VOID);

        if (looksLikeType) {
            b::ast::Type castType = parseType();
            if (check(b::lexer::TokenType::RPAREN)) {
                advance();
                auto operand = parseUnary();
                return std::make_unique<b::ast::CastExpr>(castType, std::move(operand));
            }
        }

        current = checkpoint;
    }

    if (match(b::lexer::TokenType::MINUS)) {
        auto operand = parseUnary();
        return std::make_unique<b::ast::UnaryOp>(
            b::ast::UnaryOp::Operator::NEGATE,
            std::move(operand)
        );
    }

    if (match(b::lexer::TokenType::BANG)) {
        auto operand = parseUnary();
        return std::make_unique<b::ast::UnaryOp>(
            b::ast::UnaryOp::Operator::NOT,
            std::move(operand)
        );
    }

    if (match(b::lexer::TokenType::TILDE)) {
        auto operand = parseUnary();
        return std::make_unique<b::ast::UnaryOp>(
            b::ast::UnaryOp::Operator::BITWISE_NOT,
            std::move(operand)
        );
    }

    if (match(b::lexer::TokenType::STAR)) {
        auto operand = parseUnary();
        return std::make_unique<b::ast::UnaryOp>(
            b::ast::UnaryOp::Operator::DEREF,
            std::move(operand)
        );
    }

    if (match(b::lexer::TokenType::AMPERSAND)) {
        auto operand = parseUnary();
        return std::make_unique<b::ast::UnaryOp>(
            b::ast::UnaryOp::Operator::ADDRESS_OF,
            std::move(operand)
        );
    }

    return parsePostfix();
}

std::unique_ptr<b::ast::Expression> Parser::parsePostfix() {
    auto expr = parsePrimary();

    while (true) {
        if (match(b::lexer::TokenType::LPAREN)) {
            std::vector<std::unique_ptr<b::ast::Expression>> arguments;

            if (!check(b::lexer::TokenType::RPAREN)) {
                do {
                    arguments.push_back(parseExpression());
                } while (match(b::lexer::TokenType::COMMA));
            }

            consume(b::lexer::TokenType::RPAREN, "Expected ')' after function arguments");

            if (auto* identifier = dynamic_cast<b::ast::Identifier*>(expr.get())) {
                std::string funcName = identifier->name;
                expr = std::make_unique<b::ast::FunctionCall>(funcName, std::move(arguments));
            } else {
                throw ParseException("Invalid function call");
            }
        } else if (match(b::lexer::TokenType::DOT) || match(b::lexer::TokenType::ARROW)) {
            std::string memberName = consume(b::lexer::TokenType::IDENTIFIER, "Expected member name after '.'/'->'").lexeme;
            expr = std::make_unique<b::ast::MemberAccess>(std::move(expr), memberName);
        } else if (match(b::lexer::TokenType::LBRACKET)) {
            auto indexExpr = parseExpression();
            consume(b::lexer::TokenType::RBRACKET, "Expected ']' after array index");
            expr = std::make_unique<b::ast::ArrayAccess>(std::move(expr), std::move(indexExpr));
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<b::ast::Expression> Parser::parsePrimary() {
    if (match(b::lexer::TokenType::KW_TRUE)) {
        return std::make_unique<b::ast::Literal>(b::ast::Literal::Kind::BOOLEAN, "true");
    }

    if (match(b::lexer::TokenType::KW_FALSE)) {
        return std::make_unique<b::ast::Literal>(b::ast::Literal::Kind::BOOLEAN, "false");
    }

    if (match(b::lexer::TokenType::INTEGER)) {
        return std::make_unique<b::ast::Literal>(
            b::ast::Literal::Kind::INTEGER, previous().value
        );
    }

    if (match(b::lexer::TokenType::CHAR_LITERAL)) {
        return std::make_unique<b::ast::Literal>(
            b::ast::Literal::Kind::INTEGER, previous().value
        );
    }

    if (match(b::lexer::TokenType::FLOAT)) {
        return std::make_unique<b::ast::Literal>(
            b::ast::Literal::Kind::FLOAT, previous().value
        );
    }

    if (match(b::lexer::TokenType::STRING)) {
        return std::make_unique<b::ast::Literal>(
            b::ast::Literal::Kind::STRING, previous().value
        );
    }

    if (check(b::lexer::TokenType::IDENTIFIER)) {
        auto enumIt = enumConstants.find(peek().lexeme);
        if (enumIt != enumConstants.end()) {
            advance();
            return std::make_unique<b::ast::Literal>(
                b::ast::Literal::Kind::INTEGER, std::to_string(enumIt->second)
            );
        }
    }

    if (match(b::lexer::TokenType::IDENTIFIER)) {
        return std::make_unique<b::ast::Identifier>(previous().lexeme);
    }

    if (match(b::lexer::TokenType::LPAREN)) {
        auto expr = parseExpression();
        consume(b::lexer::TokenType::RPAREN, "Expected ')' after expression");
        return expr;
    }

    throw ParseException("Unexpected token: " + peek().lexeme);
}

}

namespace b::codegen {

class CodeGenerator : public b::ast::ASTVisitor {
public:
    CodeGenerator();
    ~CodeGenerator();

    bool generate(b::ast::Program* program, const std::string& outputPath);
    bool emitObject(const std::string& outputPath);
    bool linkExecutable(const std::string& objectFile, const std::string& executable);

private:
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;
    llvm::TargetMachine* targetMachine;

    std::unordered_map<std::string, llvm::Value*> variables;
    std::unordered_map<std::string, llvm::Type*> variableTypes;
    std::unordered_map<std::string, b::ast::Type> arcVariableTypes;
    std::stack<std::unordered_map<std::string, llvm::Value*>> scopeStack;
    std::stack<std::unordered_map<std::string, llvm::Type*>> typeStack;
    std::stack<std::unordered_map<std::string, b::ast::Type>> arcTypeStack;
    std::unordered_map<std::string, llvm::StructType*> structTypes;
    std::unordered_map<std::string, std::vector<std::string>> structFields;
    std::unordered_map<std::string, llvm::FunctionType*> funcPointerTypedefs;
    std::unordered_set<std::string> constVariables;
    std::unordered_map<std::string, llvm::GlobalVariable*> globalVariables;
    llvm::Function* currentFunction;
    llvm::Value* lastValue;
    std::vector<llvm::BasicBlock*> breakTargets;
    std::vector<llvm::BasicBlock*> continueTargets;

    llvm::Type* arcTypeToLLVM(const b::ast::Type& type);
    b::ast::Type derefType(const b::ast::Type& type);
    llvm::FunctionType* createFunctionType(const b::ast::FunctionDecl* decl);
    void declareBuiltins();
    void pushScope();
    void popScope();
    void setVariable(const std::string& name, llvm::Value* value);
    llvm::Value* getVariable(const std::string& name);
    llvm::Value* getMemberFieldPtr(b::ast::MemberAccess* node, llvm::Type** outFieldType);
    bool blockIsTerminated();
    llvm::Value* toBoolCondition(llvm::Value* value);

    void visit(b::ast::Literal* node) override;
    void visit(b::ast::Identifier* node) override;
    void visit(b::ast::BinaryOp* node) override;
    void visit(b::ast::UnaryOp* node) override;
    void visit(b::ast::CastExpr* node) override;
    void visit(b::ast::FunctionCall* node) override;
    void visit(b::ast::MemberAccess* node) override;
    void visit(b::ast::ArrayAccess* node) override;
    void visit(b::ast::VariableDecl* node) override;
    void visit(b::ast::ReturnStmt* node) override;
    void visit(b::ast::ExpressionStmt* node) override;
    void visit(b::ast::Block* node) override;
    void visit(b::ast::IfStmt* node) override;
    void visit(b::ast::ForStmt* node) override;
    void visit(b::ast::WhileStmt* node) override;
    void visit(b::ast::BreakStmt* node) override;
    void visit(b::ast::ContinueStmt* node) override;
    void visit(b::ast::SwitchStmt* node) override;
    void visit(b::ast::StructDecl* node) override;
    void visit(b::ast::FunctionDecl* node) override;
    void visit(b::ast::Program* node) override;
};

CodeGenerator::CodeGenerator()
    : context(std::make_unique<llvm::LLVMContext>()),
      module(std::make_unique<llvm::Module>("b_module", *context)),
      builder(std::make_unique<llvm::IRBuilder<>>(*context)),
      targetMachine(nullptr),
      currentFunction(nullptr),
      lastValue(nullptr) {

#if defined(_WIN32)
    std::string triple = "x86_64-pc-windows-msvc";
#elif defined(__APPLE__)
    std::string triple = "x86_64-apple-darwin";
#else
    std::string triple = "x86_64-pc-linux-gnu";
#endif
#if LLVM_VERSION_MAJOR >= 21
    module->setTargetTriple(llvm::Triple(triple));
#else
    module->setTargetTriple(llvm::Triple(triple).str());
#endif
    module->setDataLayout("e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128");
    declareBuiltins();
}

CodeGenerator::~CodeGenerator() {
}

bool CodeGenerator::generate(b::ast::Program* program, const std::string& outputPath) {
    try {
        program->accept(this);

        if (llvm::verifyModule(*module, &llvm::errs())) {
            return false;
        }

        return emitObject(outputPath);
    } catch (const std::exception& ex) {
        llvm::errs() << "Codegen Error: " << ex.what() << "\n";
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

#if defined(_WIN32)
    std::string llcCommand = "llc -filetype=obj -o \"" + outputPath + "\" \"" + llFile + "\"";
#else
    std::string llcCommand = "llc -relocation-model=static -filetype=obj -o " + outputPath + " " + llFile;
#endif
    int result = std::system(llcCommand.c_str());

    std::error_code removeEc;
    fs::remove(llFile, removeEc);

    if (result != 0) {
        llvm::errs() << "llc compilation failed\n";
        return false;
    }

    return true;
}

bool CodeGenerator::linkExecutable(const std::string& objectFile,
                                    const std::string& executable) {
#if defined(_WIN32)
    std::string command = "clang++ -o \"" + executable + "\" \"" + objectFile + "\"";
#elif defined(__APPLE__)
    std::string command = "clang++ -o " + executable + " " + objectFile;
#else
    std::string command = "gcc -no-pie -o " + executable + " " + objectFile;
#endif

    int result = std::system(command.c_str());
    return result == 0;
}

b::ast::Type CodeGenerator::derefType(const b::ast::Type& type) {
    b::ast::Type result = type;
    if (result.pointerLevel > 0) {
        result.pointerLevel--;
    } else {
        throw std::runtime_error("Cannot dereference non-pointer type");
    }
    return result;
}

llvm::Type* CodeGenerator::arcTypeToLLVM(const b::ast::Type& type) {
    llvm::Type* baseType;

    if (type.isFunctionPointer()) {
        return llvm::PointerType::get(*context, 0);
    }

    if (type.isStruct()) {
        auto it = structTypes.find(type.structName);
        if (it != structTypes.end()) {
            baseType = it->second;
        } else {
            throw std::runtime_error("Unknown struct type: " + type.structName);
        }
    } else {
        switch (type.base) {
            case b::ast::PrimitiveType::INT:
                baseType = llvm::Type::getInt32Ty(*context);
                break;
            case b::ast::PrimitiveType::FLOAT:
                baseType = llvm::Type::getFloatTy(*context);
                break;
            case b::ast::PrimitiveType::DOUBLE:
                baseType = llvm::Type::getDoubleTy(*context);
                break;
            case b::ast::PrimitiveType::BOOL:
                baseType = llvm::Type::getInt1Ty(*context);
                break;
            case b::ast::PrimitiveType::CHAR:
                baseType = llvm::Type::getInt8Ty(*context);
                break;
            case b::ast::PrimitiveType::VOID:
                baseType = llvm::Type::getVoidTy(*context);
                break;
            default:
                throw std::runtime_error("Unknown type");
        }
    }

    for (int i = 0; i < type.pointerLevel; ++i) {
        (void)baseType;
        baseType = llvm::PointerType::get(*context, 0);
    }

    return baseType;
}

llvm::FunctionType* CodeGenerator::createFunctionType(
    const b::ast::FunctionDecl* decl) {

    std::vector<llvm::Type*> paramTypes;
    for (const auto& param : decl->parameters) {
        paramTypes.push_back(arcTypeToLLVM(param.type));
    }

    llvm::Type* returnType = arcTypeToLLVM(decl->returnType);
    return llvm::FunctionType::get(returnType, paramTypes, false);
}

void CodeGenerator::declareBuiltins() {
    llvm::Type* i8Ptr = llvm::PointerType::get(*context, 0);
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
        std::vector<llvm::Type*> args = {i8Ptr};
        auto funcType = llvm::FunctionType::get(i32, args, true);
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                              "scanf", module.get());
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

    {
        std::vector<llvm::Type*> args = {i8Ptr};
        auto funcType = llvm::FunctionType::get(i32, args, false);
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                              "strlen", module.get());
    }

    {
        std::vector<llvm::Type*> args = {i8Ptr, i8Ptr};
        auto funcType = llvm::FunctionType::get(i32, args, false);
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                              "strcmp", module.get());
    }

    {
        std::vector<llvm::Type*> args = {i8Ptr, i8Ptr};
        auto funcType = llvm::FunctionType::get(i8Ptr, args, false);
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                              "strcpy", module.get());
    }

    {
        std::vector<llvm::Type*> args = {i8Ptr};
        auto funcType = llvm::FunctionType::get(i32, args, false);
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                              "atoi", module.get());
    }

    {
        std::vector<llvm::Type*> args = {i8Ptr, i8Ptr};
        auto funcType = llvm::FunctionType::get(i32, args, true);
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage,
                              "sprintf", module.get());
    }
}

bool CodeGenerator::blockIsTerminated() {
    llvm::BasicBlock* block = builder->GetInsertBlock();
    return block != nullptr && block->getTerminator() != nullptr;
}

llvm::Value* CodeGenerator::toBoolCondition(llvm::Value* value) {
    llvm::Type* type = value->getType();
    if (type->isIntegerTy(1)) {
        return value;
    }
    if (type->isIntegerTy()) {
        return builder->CreateICmpNE(value, llvm::ConstantInt::get(type, 0));
    }
    if (type->isFloatingPointTy()) {
        return builder->CreateFCmpONE(value, llvm::ConstantFP::get(type, 0.0));
    }
    if (type->isPointerTy()) {
        return builder->CreateICmpNE(value, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(type)));
    }
    return value;
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

void CodeGenerator::visit(b::ast::Literal* node) {
    switch (node->kind) {
        case b::ast::Literal::Kind::INTEGER: {
            int value = std::stoi(node->value);
            lastValue = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), value);
            break;
        }
        case b::ast::Literal::Kind::FLOAT: {
            float value = std::stof(node->value);
            lastValue = llvm::ConstantFP::get(llvm::Type::getFloatTy(*context), value);
            break;
        }
        case b::ast::Literal::Kind::STRING: {
            lastValue = builder->CreateGlobalString(node->value);
            break;
        }
        case b::ast::Literal::Kind::BOOLEAN: {
            bool value = (node->value == "true");
            lastValue = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), value);
            break;
        }
    }
}

void CodeGenerator::visit(b::ast::Identifier* node) {
    auto globalIt = globalVariables.find(node->name);
    if (globalIt != globalVariables.end()) {
        llvm::GlobalVariable* gv = globalIt->second;
        auto typeIt = variableTypes.find(node->name);
        if (typeIt != variableTypes.end()) {
            lastValue = builder->CreateLoad(typeIt->second, gv);
        } else {
            lastValue = builder->CreateLoad(gv->getValueType(), gv);
        }
        return;
    }

    if (variables.find(node->name) == variables.end()) {
        llvm::Function* func = module->getFunction(node->name);
        if (func) {
            lastValue = func;
            return;
        }
    }

    llvm::Value* varPtr = getVariable(node->name);
    auto it = variableTypes.find(node->name);
    if (it != variableTypes.end()) {
        lastValue = builder->CreateLoad(it->second, varPtr);
    } else {
        lastValue = varPtr;
    }
}

void CodeGenerator::visit(b::ast::BinaryOp* node) {
    if (node->op == b::ast::BinaryOp::Operator::ASSIGN) {
        node->right->accept(this);
        llvm::Value* rhsValue = lastValue;

        if (auto* ident = dynamic_cast<b::ast::Identifier*>(node->left.get())) {
            if (constVariables.count(ident->name)) {
                throw b::CompilerException("Cannot assign to const variable: " + ident->name);
            }
            auto globalIt = globalVariables.find(ident->name);
            if (globalIt != globalVariables.end()) {
                builder->CreateStore(rhsValue, globalIt->second);
                lastValue = rhsValue;
            } else {
                llvm::Value* lhsAddr = getVariable(ident->name);
                builder->CreateStore(rhsValue, lhsAddr);
                lastValue = rhsValue;
            }
        } else if (auto* arrayAccess = dynamic_cast<b::ast::ArrayAccess*>(node->left.get())) {
            arrayAccess->array->accept(this);
            llvm::Value* arrayPtr = lastValue;

            arrayAccess->index->accept(this);
            llvm::Value* indexVal = lastValue;

            auto arcTypeIt = arcVariableTypes.find(
                dynamic_cast<b::ast::Identifier*>(arrayAccess->array.get()) ?
                dynamic_cast<b::ast::Identifier*>(arrayAccess->array.get())->name : ""
            );

            if (arcTypeIt != arcVariableTypes.end()) {
                b::ast::Type elemType = derefType(arcTypeIt->second);
                llvm::Type* elemLLVMType = arcTypeToLLVM(elemType);
                llvm::Value* elemPtr = builder->CreateGEP(elemLLVMType, arrayPtr, indexVal);
                builder->CreateStore(rhsValue, elemPtr);
                lastValue = rhsValue;
            } else {
                throw std::runtime_error("Cannot determine array element type");
            }
        } else if (auto* memberAccess = dynamic_cast<b::ast::MemberAccess*>(node->left.get())) {
            llvm::Type* fieldType = nullptr;
            llvm::Value* elementPtr = getMemberFieldPtr(memberAccess, &fieldType);

            if (rhsValue->getType() != fieldType) {
                if (rhsValue->getType()->isIntegerTy() && fieldType->isIntegerTy()) {
                    rhsValue = builder->CreateIntCast(rhsValue, fieldType, true);
                }
            }

            builder->CreateStore(rhsValue, elementPtr);
            lastValue = rhsValue;
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
    } else if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy() &&
               lhs->getType() != rhs->getType()) {
        unsigned lhsWidth = lhs->getType()->getIntegerBitWidth();
        unsigned rhsWidth = rhs->getType()->getIntegerBitWidth();
        if (lhsWidth < rhsWidth) {
            lhs = builder->CreateIntCast(lhs, rhs->getType(), true);
        } else {
            rhs = builder->CreateIntCast(rhs, lhs->getType(), true);
        }
    } else if (lhs->getType()->isFloatingPointTy() && rhs->getType()->isIntegerTy()) {
        rhs = builder->CreateSIToFP(rhs, lhs->getType());
    } else if (lhs->getType()->isIntegerTy() && rhs->getType()->isFloatingPointTy()) {
        lhs = builder->CreateSIToFP(lhs, rhs->getType());
    } else if (lhs->getType()->isFloatingPointTy() && rhs->getType()->isFloatingPointTy() &&
               lhs->getType() != rhs->getType()) {
        if (lhs->getType()->getPrimitiveSizeInBits() < rhs->getType()->getPrimitiveSizeInBits()) {
            lhs = builder->CreateFPExt(lhs, rhs->getType());
        } else {
            rhs = builder->CreateFPExt(rhs, lhs->getType());
        }
    }

    bool isFloat = lhs->getType()->isFloatingPointTy();

    switch (node->op) {
        case b::ast::BinaryOp::Operator::PLUS:
            lastValue = isFloat ? builder->CreateFAdd(lhs, rhs) : builder->CreateAdd(lhs, rhs);
            break;
        case b::ast::BinaryOp::Operator::MINUS:
            lastValue = isFloat ? builder->CreateFSub(lhs, rhs) : builder->CreateSub(lhs, rhs);
            break;
        case b::ast::BinaryOp::Operator::MULTIPLY:
            lastValue = isFloat ? builder->CreateFMul(lhs, rhs) : builder->CreateMul(lhs, rhs);
            break;
        case b::ast::BinaryOp::Operator::DIVIDE:
            lastValue = isFloat ? builder->CreateFDiv(lhs, rhs) : builder->CreateSDiv(lhs, rhs);
            break;
        case b::ast::BinaryOp::Operator::MODULO:
            lastValue = isFloat ? builder->CreateFRem(lhs, rhs) : builder->CreateSRem(lhs, rhs);
            break;
        case b::ast::BinaryOp::Operator::EQUAL:
            lastValue = isFloat ? builder->CreateFCmpOEQ(lhs, rhs) : builder->CreateICmpEQ(lhs, rhs);
            break;
        case b::ast::BinaryOp::Operator::NOT_EQUAL:
            lastValue = isFloat ? builder->CreateFCmpONE(lhs, rhs) : builder->CreateICmpNE(lhs, rhs);
            break;
        case b::ast::BinaryOp::Operator::LESS:
            lastValue = isFloat ? builder->CreateFCmpOLT(lhs, rhs) : builder->CreateICmpSLT(lhs, rhs);
            break;
        case b::ast::BinaryOp::Operator::LESS_EQUAL:
            lastValue = isFloat ? builder->CreateFCmpOLE(lhs, rhs) : builder->CreateICmpSLE(lhs, rhs);
            break;
        case b::ast::BinaryOp::Operator::GREATER:
            lastValue = isFloat ? builder->CreateFCmpOGT(lhs, rhs) : builder->CreateICmpSGT(lhs, rhs);
            break;
        case b::ast::BinaryOp::Operator::GREATER_EQUAL:
            lastValue = isFloat ? builder->CreateFCmpOGE(lhs, rhs) : builder->CreateICmpSGE(lhs, rhs);
            break;
        case b::ast::BinaryOp::Operator::LOGICAL_AND:
            lastValue = builder->CreateAnd(toBoolCondition(lhs), toBoolCondition(rhs));
            break;
        case b::ast::BinaryOp::Operator::LOGICAL_OR:
            lastValue = builder->CreateOr(toBoolCondition(lhs), toBoolCondition(rhs));
            break;
        case b::ast::BinaryOp::Operator::BITWISE_AND:
            lastValue = builder->CreateAnd(lhs, rhs);
            break;
        case b::ast::BinaryOp::Operator::BITWISE_OR:
            lastValue = builder->CreateOr(lhs, rhs);
            break;
        case b::ast::BinaryOp::Operator::BITWISE_XOR:
            lastValue = builder->CreateXor(lhs, rhs);
            break;
        default:
            throw std::runtime_error("Unknown binary operator");
    }
}

void CodeGenerator::visit(b::ast::UnaryOp* node) {
    node->operand->accept(this);
    llvm::Value* operand = lastValue;

    switch (node->op) {
        case b::ast::UnaryOp::Operator::NEGATE:
            lastValue = operand->getType()->isFloatingPointTy()
                ? builder->CreateFNeg(operand)
                : builder->CreateNeg(operand);
            break;
        case b::ast::UnaryOp::Operator::NOT:
            lastValue = builder->CreateNot(toBoolCondition(operand));
            break;
        case b::ast::UnaryOp::Operator::BITWISE_NOT:
            lastValue = builder->CreateNot(operand);
            break;
        case b::ast::UnaryOp::Operator::DEREF: {
            if (auto* ident = dynamic_cast<b::ast::Identifier*>(node->operand.get())) {
                auto arcTypeIt = arcVariableTypes.find(ident->name);
                if (arcTypeIt == arcVariableTypes.end()) {
                    throw std::runtime_error("Unknown variable: " + ident->name);
                }

                b::ast::Type derefArcType = derefType(arcTypeIt->second);
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
        case b::ast::UnaryOp::Operator::ADDRESS_OF: {
            if (auto* ident = dynamic_cast<b::ast::Identifier*>(node->operand.get())) {
                lastValue = getVariable(ident->name);
            } else {
                throw std::runtime_error("Cannot take address of non-lvalue");
            }
            break;
        }
    }
}

void CodeGenerator::visit(b::ast::CastExpr* node) {
    node->expr->accept(this);
    llvm::Value* value = lastValue;

    llvm::Type* targetLLVMType = arcTypeToLLVM(node->targetType);
    llvm::Type* sourceType = value->getType();

    if (sourceType == targetLLVMType) {
        lastValue = value;
        return;
    }

    if (sourceType->isIntegerTy() && targetLLVMType->isIntegerTy()) {
        if (sourceType->getIntegerBitWidth() < targetLLVMType->getIntegerBitWidth()) {
            lastValue = builder->CreateSExt(value, targetLLVMType);
        } else {
            lastValue = builder->CreateTrunc(value, targetLLVMType);
        }
    } else if (sourceType->isIntegerTy() && targetLLVMType->isFloatingPointTy()) {
        lastValue = builder->CreateSIToFP(value, targetLLVMType);
    } else if (sourceType->isFloatingPointTy() && targetLLVMType->isIntegerTy()) {
        lastValue = builder->CreateFPToSI(value, targetLLVMType);
    } else if (sourceType->isFloatingPointTy() && targetLLVMType->isFloatingPointTy()) {
        if (sourceType->getPrimitiveSizeInBits() < targetLLVMType->getPrimitiveSizeInBits()) {
            lastValue = builder->CreateFPExt(value, targetLLVMType);
        } else {
            lastValue = builder->CreateFPTrunc(value, targetLLVMType);
        }
    } else if (sourceType->isIntegerTy() && targetLLVMType->isPointerTy()) {
        lastValue = builder->CreateIntToPtr(value, targetLLVMType);
    } else if (sourceType->isPointerTy() && targetLLVMType->isIntegerTy()) {
        lastValue = builder->CreatePtrToInt(value, targetLLVMType);
    } else if (sourceType->isPointerTy() && targetLLVMType->isPointerTy()) {
        lastValue = value;
    } else {
        throw std::runtime_error("Invalid type cast");
    }
}

void CodeGenerator::visit(b::ast::FunctionCall* node) {
    if (node->functionName == "print" || node->functionName == "println") {
        llvm::Function* printfFunc = module->getFunction("printf");
        if (!printfFunc) {
            throw std::runtime_error("printf not declared");
        }

        std::vector<llvm::Value*> printArgs;
        std::string format;

        for (size_t i = 0; i < node->arguments.size(); ++i) {
            node->arguments[i]->accept(this);
            llvm::Value* argValue = lastValue;
            llvm::Type* argType = argValue->getType();

            if (argType->isPointerTy()) {
                format += "%s";
            } else if (argType->isIntegerTy(1)) {
                format += "%d";
                argValue = builder->CreateZExt(argValue, llvm::Type::getInt32Ty(*context));
            } else if (argType->isIntegerTy()) {
                format += "%d";
            } else if (argType->isFloatingPointTy()) {
                format += "%f";
                if (!argType->isDoubleTy()) {
                    argValue = builder->CreateFPExt(argValue, llvm::Type::getDoubleTy(*context));
                }
            } else {
                format += "%s";
            }

            printArgs.push_back(argValue);
        }

        if (node->functionName == "println") {
            format += "\n";
        }

        llvm::Value* formatStr = builder->CreateGlobalString(format);
        printArgs.insert(printArgs.begin(), formatStr);

        lastValue = builder->CreateCall(printfFunc, printArgs);
        return;
    }

    if (node->functionName == "itoa") {
        if (node->arguments.size() != 1) {
            throw std::runtime_error("itoa expects exactly 1 argument");
        }

        llvm::Function* mallocFunc = module->getFunction("malloc");
        llvm::Function* sprintfFunc = module->getFunction("sprintf");
        if (!mallocFunc || !sprintfFunc) {
            throw std::runtime_error("malloc/sprintf not declared");
        }

        node->arguments[0]->accept(this);
        llvm::Value* intValue = lastValue;
        if (!intValue->getType()->isIntegerTy(32)) {
            intValue = builder->CreateIntCast(intValue, llvm::Type::getInt32Ty(*context), true);
        }

        llvm::Value* bufSize = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 12);
        llvm::Value* buffer = builder->CreateCall(mallocFunc, {bufSize});

        llvm::Value* formatStr = builder->CreateGlobalString("%d");
        builder->CreateCall(sprintfFunc, {buffer, formatStr, intValue});

        lastValue = buffer;
        return;
    }

    llvm::Value* calleeValue = nullptr;
    llvm::FunctionType* funcType = nullptr;
    llvm::Function* func = nullptr;

    auto arcTypeIt = arcVariableTypes.find(node->functionName);
    if (arcTypeIt != arcVariableTypes.end() && arcTypeIt->second.isFunctionPointer()) {
        auto typedefIt = funcPointerTypedefs.find(arcTypeIt->second.funcPointerTypedefName);
        if (typedefIt == funcPointerTypedefs.end()) {
            throw std::runtime_error("Unknown function pointer type for: " + node->functionName);
        }
        funcType = typedefIt->second;
        llvm::Value* varPtr = getVariable(node->functionName);
        calleeValue = builder->CreateLoad(llvm::PointerType::get(*context, 0), varPtr);
    } else {
        func = module->getFunction(node->functionName);
        if (!func) {
            throw std::runtime_error("Undefined function: " + node->functionName);
        }
        funcType = func->getFunctionType();
        calleeValue = func;
    }

    std::vector<llvm::Value*> args;

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
        } else if (funcType->isVarArg()) {
            if (argValue->getType()->isFloatTy()) {
                argValue = builder->CreateFPExt(argValue, llvm::Type::getDoubleTy(*context));
            } else if (argValue->getType()->isIntegerTy() &&
                       argValue->getType()->getIntegerBitWidth() < 32) {
                argValue = builder->CreateIntCast(argValue, llvm::Type::getInt32Ty(*context), true);
            }
        }
        args.push_back(argValue);
    }

    lastValue = builder->CreateCall(funcType, calleeValue, args);
}

llvm::Value* CodeGenerator::getMemberFieldPtr(b::ast::MemberAccess* node, llvm::Type** outFieldType) {
    auto* ident = dynamic_cast<b::ast::Identifier*>(node->object.get());
    if (!ident) {
        throw std::runtime_error("Invalid member access");
    }

    auto it = variableTypes.find(ident->name);
    if (it == variableTypes.end()) {
        throw std::runtime_error("Unknown variable type: " + ident->name);
    }

    llvm::Type* objectType = it->second;
    llvm::StructType* structType = nullptr;
    llvm::Value* objectPtr = nullptr;

    if (auto* st = llvm::dyn_cast<llvm::StructType>(objectType)) {
        structType = st;
        objectPtr = getVariable(ident->name);
    } else if (auto* pt = llvm::dyn_cast<llvm::PointerType>(objectType)) {
        (void)pt;
        auto arcIt = arcVariableTypes.find(ident->name);
        if (arcIt != arcVariableTypes.end()) {
            b::ast::Type elemType = this->derefType(arcIt->second);
            if (elemType.isStruct()) {
                auto structTypeIt = structTypes.find(elemType.structName);
                if (structTypeIt != structTypes.end()) {
                    structType = structTypeIt->second;
                }
            }
        }
        node->object->accept(this);
        objectPtr = lastValue;
    }

    if (!structType) {
        throw std::runtime_error("Object is not a struct type");
    }

    auto structIt = structFields.find(structType->getName().str());
    if (structIt == structFields.end()) {
        throw std::runtime_error("Unknown struct type: " + structType->getName().str());
    }

    unsigned fieldIndex = 0;
    bool found = false;
    for (unsigned i = 0; i < structIt->second.size(); ++i) {
        if (structIt->second[i] == node->member) {
            fieldIndex = i;
            found = true;
            break;
        }
    }

    if (!found) {
        throw std::runtime_error("Unknown struct field: " + node->member);
    }

    *outFieldType = structType->getElementType(fieldIndex);
    return builder->CreateStructGEP(structType, objectPtr, fieldIndex);
}

void CodeGenerator::visit(b::ast::MemberAccess* node) {
    llvm::Type* fieldType = nullptr;
    llvm::Value* elementPtr = getMemberFieldPtr(node, &fieldType);
    lastValue = builder->CreateLoad(fieldType, elementPtr);
}

void CodeGenerator::visit(b::ast::ArrayAccess* node) {
    node->array->accept(this);
    llvm::Value* arrayPtr = lastValue;

    node->index->accept(this);
    llvm::Value* indexVal = lastValue;

    auto arcTypeIt = arcVariableTypes.find(
        dynamic_cast<b::ast::Identifier*>(node->array.get()) ?
        dynamic_cast<b::ast::Identifier*>(node->array.get())->name : ""
    );

    if (arcTypeIt != arcVariableTypes.end()) {
        b::ast::Type elemType = derefType(arcTypeIt->second);
        llvm::Type* elemLLVMType = arcTypeToLLVM(elemType);
        llvm::Value* elemPtr = builder->CreateGEP(elemLLVMType, arrayPtr, indexVal);
        lastValue = builder->CreateLoad(elemLLVMType, elemPtr);
    } else {
        throw std::runtime_error("Cannot determine array element type");
    }
}

void CodeGenerator::visit(b::ast::VariableDecl* node) {
    llvm::Type* type = arcTypeToLLVM(node->type);
    llvm::AllocaInst* alloca = builder->CreateAlloca(type);

    if (node->initializer) {
        node->initializer->accept(this);
        llvm::Value* initValue = lastValue;

        if (initValue->getType() != type) {
            if (initValue->getType()->isIntegerTy() && type->isIntegerTy()) {
                initValue = builder->CreateIntCast(initValue, type, true);
            } else if (initValue->getType()->isIntegerTy() && type->isFloatingPointTy()) {
                initValue = builder->CreateSIToFP(initValue, type);
            } else if (initValue->getType()->isFloatingPointTy() && type->isIntegerTy()) {
                initValue = builder->CreateFPToSI(initValue, type);
            } else if (initValue->getType()->isFloatingPointTy() && type->isFloatingPointTy()) {
                initValue = builder->CreateFPCast(initValue, type);
            } else if (initValue->getType()->isIntegerTy() && type->isPointerTy()) {
                if (auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(initValue)) {
                    if (constInt->isZero()) {
                        initValue = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(type));
                    }
                }
            }
        }

        builder->CreateStore(initValue, alloca);
    }

    setVariable(node->name, alloca);
    variableTypes[node->name] = type;
    arcVariableTypes[node->name] = node->type;
    if (node->isConst) {
        constVariables.insert(node->name);
    }
}

void CodeGenerator::visit(b::ast::ReturnStmt* node) {
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

void CodeGenerator::visit(b::ast::ExpressionStmt* node) {
    node->expression->accept(this);
}

void CodeGenerator::visit(b::ast::Block* node) {
    for (const auto& stmt : node->statements) {
        if (blockIsTerminated()) {
            break;
        }
        stmt->accept(this);
    }
}

void CodeGenerator::visit(b::ast::IfStmt* node) {
    node->condition->accept(this);
    llvm::Value* condition = toBoolCondition(lastValue);

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
    if (!blockIsTerminated()) {
        builder->CreateBr(mergeBlock);
    }

    if (node->elseBranch) {
        builder->SetInsertPoint(elseBlock);
        node->elseBranch->accept(this);
        if (!blockIsTerminated()) {
            builder->CreateBr(mergeBlock);
        }
    }

    builder->SetInsertPoint(mergeBlock);
}

void CodeGenerator::visit(b::ast::ForStmt* node) {
    llvm::BasicBlock* condBlock = llvm::BasicBlock::Create(*context, "forcond", currentFunction);
    llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(*context, "forbody", currentFunction);
    llvm::BasicBlock* incrementBlock = llvm::BasicBlock::Create(*context, "forinc", currentFunction);
    llvm::BasicBlock* afterBlock = llvm::BasicBlock::Create(*context, "forend", currentFunction);

    pushScope();

    if (node->init) {
        node->init->accept(this);
    }

    builder->CreateBr(condBlock);
    builder->SetInsertPoint(condBlock);

    if (node->condition) {
        node->condition->accept(this);
        builder->CreateCondBr(toBoolCondition(lastValue), bodyBlock, afterBlock);
    } else {
        builder->CreateBr(bodyBlock);
    }

    breakTargets.push_back(afterBlock);
    continueTargets.push_back(incrementBlock);

    builder->SetInsertPoint(bodyBlock);
    node->body->accept(this);
    if (!blockIsTerminated()) {
        builder->CreateBr(incrementBlock);
    }

    breakTargets.pop_back();
    continueTargets.pop_back();

    builder->SetInsertPoint(incrementBlock);
    if (node->increment) {
        node->increment->accept(this);
    }
    builder->CreateBr(condBlock);

    builder->SetInsertPoint(afterBlock);

    popScope();
}

void CodeGenerator::visit(b::ast::WhileStmt* node) {
    llvm::BasicBlock* condBlock = llvm::BasicBlock::Create(*context, "whilecond", currentFunction);
    llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(*context, "whilebody", currentFunction);
    llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(*context, "whileend", currentFunction);

    builder->CreateBr(condBlock);
    builder->SetInsertPoint(condBlock);

    node->condition->accept(this);
    builder->CreateCondBr(toBoolCondition(lastValue), bodyBlock, endBlock);

    breakTargets.push_back(endBlock);
    continueTargets.push_back(condBlock);

    builder->SetInsertPoint(bodyBlock);
    node->body->accept(this);
    if (!blockIsTerminated()) {
        builder->CreateBr(condBlock);
    }

    breakTargets.pop_back();
    continueTargets.pop_back();

    builder->SetInsertPoint(endBlock);
}

void CodeGenerator::visit(b::ast::BreakStmt* node) {
    (void)node;
    if (breakTargets.empty()) {
        throw std::runtime_error("'break' used outside of a loop");
    }
    builder->CreateBr(breakTargets.back());
}

void CodeGenerator::visit(b::ast::ContinueStmt* node) {
    (void)node;
    if (continueTargets.empty()) {
        throw std::runtime_error("'continue' used outside of a loop");
    }
    builder->CreateBr(continueTargets.back());
}

void CodeGenerator::visit(b::ast::SwitchStmt* node) {
    node->condition->accept(this);
    llvm::Value* switchValue = lastValue;

    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(*context, "switch.merge", currentFunction);
    llvm::SwitchInst* switchInst = builder->CreateSwitch(switchValue, mergeBlock, node->cases.size());

    llvm::BasicBlock* defaultBlock = nullptr;

    for (const auto& caseItem : node->cases) {
        llvm::BasicBlock* caseBlock = llvm::BasicBlock::Create(*context, "switch.case", currentFunction);
        builder->SetInsertPoint(caseBlock);

        breakTargets.push_back(mergeBlock);

        for (const auto& stmt : caseItem.statements) {
            stmt->accept(this);
        }

        if (!blockIsTerminated()) {
            builder->CreateBr(mergeBlock);
        }

        breakTargets.pop_back();

        if (caseItem.isDefault) {
            defaultBlock = caseBlock;
            switchInst->setDefaultDest(caseBlock);
        } else {
            caseItem.value->accept(this);
            llvm::Value* caseValue = lastValue;
            if (auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(caseValue)) {
                switchInst->addCase(constInt, caseBlock);
            }
        }
    }

    if (!defaultBlock) {
        switchInst->setDefaultDest(mergeBlock);
    }

    builder->SetInsertPoint(mergeBlock);
}

void CodeGenerator::visit(b::ast::FunctionDecl* node) {
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

    if (node->returnType.isVoid() && !blockIsTerminated()) {
        builder->CreateRetVoid();
    }
}

void CodeGenerator::visit(b::ast::StructDecl* node) {
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

void CodeGenerator::visit(b::ast::Program* node) {
    for (const auto& strct : node->structs) {
        strct->accept(this);
    }

    for (const auto& typedefDecl : node->funcPointerTypedefs) {
        std::vector<llvm::Type*> paramTypes;
        for (const auto& paramType : typedefDecl.paramTypes) {
            paramTypes.push_back(arcTypeToLLVM(paramType));
        }
        llvm::Type* returnType = arcTypeToLLVM(typedefDecl.returnType);
        funcPointerTypedefs[typedefDecl.name] = llvm::FunctionType::get(returnType, paramTypes, false);
    }

    for (const auto& globalVar : node->globalVariables) {
        llvm::Type* llvmType = arcTypeToLLVM(globalVar.type);
        llvm::Constant* initializer = nullptr;
        if (globalVar.initializer) {
            globalVar.initializer->accept(this);
            initializer = llvm::dyn_cast<llvm::Constant>(lastValue);
            if (!initializer) {
                if (globalVar.type.base == b::ast::PrimitiveType::INT) {
                    initializer = llvm::ConstantInt::get(llvmType, 0);
                } else if (globalVar.type.base == b::ast::PrimitiveType::FLOAT ||
                           globalVar.type.base == b::ast::PrimitiveType::DOUBLE) {
                    initializer = llvm::ConstantFP::get(llvmType, 0.0);
                } else if (globalVar.type.base == b::ast::PrimitiveType::BOOL) {
                    initializer = llvm::ConstantInt::get(llvmType, 0);
                } else {
                    initializer = llvm::Constant::getNullValue(llvmType);
                }
            }
        } else {
            initializer = llvm::Constant::getNullValue(llvmType);
        }

        auto gv = new llvm::GlobalVariable(*module, llvmType, globalVar.isConst,
                                           llvm::GlobalValue::InternalLinkage, initializer, globalVar.name);
        globalVariables[globalVar.name] = gv;
        if (globalVar.isConst) {
            constVariables.insert(globalVar.name);
        }
    }

    for (const auto& func : node->functions) {
        func->accept(this);
    }
}

}

std::string readFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw b::CompilerException("Cannot open file: " + filepath);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " <source.arc>" << std::endl;
}

void printVersion() {
    std::cout << "B 1.0.0" << std::endl;
}

class ASTPrinter : public b::ast::ASTVisitor {
public:
    void visit(b::ast::Literal* node) override {
        std::cout << "Literal(" << node->value << ")";
    }

    void visit(b::ast::Identifier* node) override {
        std::cout << "Identifier(" << node->name << ")";
    }

    void visit(b::ast::BinaryOp* node) override {
        std::cout << "BinaryOp(";
        node->left->accept(this);
        std::cout << ", ";
        node->right->accept(this);
        std::cout << ")";
    }

    void visit(b::ast::UnaryOp* node) override {
        std::cout << "UnaryOp(";
        node->operand->accept(this);
        std::cout << ")";
    }

    void visit(b::ast::CastExpr* node) override {
        std::cout << "Cast(";
        node->expr->accept(this);
        std::cout << ")";
    }

    void visit(b::ast::FunctionCall* node) override {
        std::cout << "Call(" << node->functionName << ")";
    }

    void visit(b::ast::MemberAccess* node) override {
        std::cout << "MemberAccess(." << node->member << ")";
    }

    void visit(b::ast::ArrayAccess* node) override {
        std::cout << "ArrayAccess([...])";
    }

    void visit(b::ast::VariableDecl* node) override {
        std::cout << "VarDecl(" << node->name << ")";
    }

    void visit(b::ast::ReturnStmt* node) override {
        (void)node;
        std::cout << "Return";
    }

    void visit(b::ast::ExpressionStmt* node) override {
        (void)node;
        std::cout << "ExprStmt";
    }

    void visit(b::ast::Block* node) override {
        std::cout << "Block(" << node->statements.size() << " stmts)";
    }

    void visit(b::ast::IfStmt* node) override {
        (void)node;
        std::cout << "If";
    }

    void visit(b::ast::ForStmt* node) override {
        (void)node;
        std::cout << "For";
    }

    void visit(b::ast::WhileStmt* node) override {
        (void)node;
        std::cout << "While";
    }

    void visit(b::ast::BreakStmt* node) override {
        (void)node;
        std::cout << "Break";
    }

    void visit(b::ast::ContinueStmt* node) override {
        (void)node;
        std::cout << "Continue";
    }

    void visit(b::ast::SwitchStmt* node) override {
        (void)node;
        std::cout << "Switch";
    }

    void visit(b::ast::StructDecl* node) override {
        std::cout << "Struct(" << node->name << ")";
    }

    void visit(b::ast::FunctionDecl* node) override {
        std::cout << "Function(" << node->name << ")";
    }

    void visit(b::ast::Program* node) override {
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

    if (firstArg == "--update") {
        std::cout << "Updating Arc..." << std::endl;
#if defined(_WIN32)
        std::string command =
            "powershell -NoProfile -ExecutionPolicy Bypass -Command "
            "\"if (Test-Path \\\"$env:USERPROFILE\\.arc\\repo\\get.ps1\\\") { "
            "powershell -ExecutionPolicy Bypass -File "
            "\\\"$env:USERPROFILE\\.arc\\repo\\get.ps1\\\" -Action update "
            "} else { irm https://raw.githubusercontent.com/ital87/Arc/1.0.0/get.ps1 | iex }\"";
#else
        std::string command =
            "bash -c 'if [ -f \"$HOME/.arc/repo/get.sh\" ]; then "
            "bash \"$HOME/.arc/repo/get.sh\" update; "
            "else "
            "curl -fsSL https://raw.githubusercontent.com/ital87/Arc/1.0.0/get.sh | bash; "
            "fi'";
#endif
        int result = std::system(command.c_str());
        return result == 0 ? 0 : 1;
    }

    std::string filepath = firstArg;
    bool debugMode = (argc > 2 && std::string(argv[2]) == "--debug");

    try {
        std::cout << "[1/4] Reading source file..." << std::endl;
        std::string source = readFile(filepath);

        std::cout << "[2/4] Lexing..." << std::endl;
        b::lexer::Lexer lexer(source);
        auto tokens = lexer.tokenize();
        std::cout << "      Tokens: " << tokens.size() - 1 << std::endl;

        std::cout << "[3/4] Parsing..." << std::endl;
        b::parser::Parser parser(tokens);
        auto program = parser.parse();
        std::cout << "      Functions: " << program->functions.size() << std::endl;

        if (debugMode) {
            std::cout << "      AST:" << std::endl;
            ASTPrinter printer;
            program->accept(&printer);
            std::cout << std::endl;
        }

        std::cout << "[4/4] Code generation..." << std::endl;

        b::codegen::CodeGenerator codegen;

#if defined(_WIN32)
        std::string objectFile = fs::path(filepath).stem().string() + ".obj";
        std::string executable = fs::path(filepath).stem().string() + ".exe";
#else
        std::string objectFile = fs::path(filepath).stem().string() + ".o";
        std::string executable = fs::path(filepath).stem().string();
#endif

        if (!codegen.generate(program.get(), objectFile)) {
            std::cerr << "Code generation failed" << std::endl;
            return 1;
        }

        if (!codegen.linkExecutable(objectFile, executable)) {
            std::cerr << "Linking failed" << std::endl;
            return 1;
        }

        std::error_code removeEc;
        fs::remove(objectFile, removeEc);

        std::cout << "      Executable: " << executable << std::endl;

        std::cout << "\n=== Compilation successful ===" << std::endl;
#if defined(_WIN32)
        std::cout << "Run with: .\\" << executable << std::endl;
#else
        std::cout << "Run with: ./" << executable << std::endl;
#endif

        return 0;

    } catch (const b::CompilerException& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
}

