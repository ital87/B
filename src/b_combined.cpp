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
#include <algorithm>
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
#include <llvm/IR/MDBuilder.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/AsmParser/Parser.h>
#include <llvm/Linker/Linker.h>

#if !defined(__linux__) || !defined(__x86_64__)
#error "B currently targets Linux on x86-64 only. Its runtime allocator and I/O \
issue Linux syscalls directly, so a build for any other target would produce \
programs that fault on the first allocation. Porting means giving b_os_alloc, \
b_os_release, b_write, b_read, b_open, b_close and b_panic an implementation \
for the target platform."
#endif

namespace fs = std::filesystem;

namespace b {

class CompilerException : public std::runtime_error {
public:
    explicit CompilerException(const std::string& message)
        : std::runtime_error(message) {
    }
};

}

namespace b::diag {

enum class Severity {
    Error,
    Warning,
    Note,
};

struct Message {
    Severity severity = Severity::Error;
    std::string file;
    int line = 0;
    int column = 0;
    std::string text;
    std::string help;
};

inline size_t editDistance(const std::string& a, const std::string& b) {
    std::vector<size_t> previous(b.size() + 1);
    std::vector<size_t> current(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) {
        previous[j] = j;
    }
    for (size_t i = 1; i <= a.size(); ++i) {
        current[0] = i;
        for (size_t j = 1; j <= b.size(); ++j) {
            size_t substitution = previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, substitution});
        }
        previous = current;
    }
    return previous[b.size()];
}

inline std::string closestMatch(const std::string& name, const std::vector<std::string>& candidates) {
    std::string best;
    size_t bestDistance = 0;
    size_t limit = std::max<size_t>(1, name.size() / 3 + 1);
    for (const auto& candidate : candidates) {
        size_t distance = editDistance(name, candidate);
        if (distance <= limit && (best.empty() || distance < bestDistance)) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

class Reporter {
public:
    void report(Message message) {
        if (message.severity == Severity::Error) {
            ++errorCount;
        } else if (message.severity == Severity::Warning) {
            ++warningCount;
        }
        messages.push_back(std::move(message));
    }

    void error(const std::string& file, int line, int column, const std::string& text,
               const std::string& help = "") {
        report({Severity::Error, file, line, column, text, help});
    }

    void warning(const std::string& file, int line, int column, const std::string& text,
                 const std::string& help = "") {
        report({Severity::Warning, file, line, column, text, help});
    }

    bool failed() const { return errorCount > 0; }
    size_t errors() const { return errorCount; }
    size_t warnings() const { return warningCount; }
    bool empty() const { return messages.empty(); }

    void addSource(const std::string& file, const std::string& text) { sources[file] = text; }

    void print(std::ostream& out) const;

private:
    std::vector<Message> messages;
    std::unordered_map<std::string, std::string> sources;
    size_t errorCount = 0;
    size_t warningCount = 0;

    std::string sourceLine(const std::string& file, int line) const;
};

std::string Reporter::sourceLine(const std::string& file, int line) const {
    auto it = sources.find(file);
    if (it == sources.end() || line <= 0) {
        return "";
    }
    std::istringstream stream(it->second);
    std::string text;
    for (int i = 0; i < line && std::getline(stream, text); ++i) {
        if (i == line - 1) {
            return text;
        }
    }
    return "";
}

void Reporter::print(std::ostream& out) const {
    for (const auto& message : messages) {
        const char* label = message.severity == Severity::Error     ? "error"
                            : message.severity == Severity::Warning ? "warning"
                                                                    : "note";
        if (!message.file.empty() && message.line > 0) {
            out << message.file << ":" << message.line << ":" << message.column << ": ";
        }
        out << label << ": " << message.text << "\n";

        std::string source = sourceLine(message.file, message.line);
        if (!source.empty()) {
            std::string gutter = std::to_string(message.line);
            out << "  " << gutter << " | " << source << "\n";
            out << "  " << std::string(gutter.size(), ' ') << " | ";
            for (int i = 1; i < message.column; ++i) {
                out << (i - 1 < static_cast<int>(source.size()) && source[i - 1] == '\t' ? '\t' : ' ');
            }
            out << "^\n";
        }
        if (!message.help.empty()) {
            out << "  help: " << message.help << "\n";
        }
        out << "\n";
    }

    if (errorCount > 0 || warningCount > 0) {
        out << errorCount << " error" << (errorCount == 1 ? "" : "s") << ", " << warningCount
            << " warning" << (warningCount == 1 ? "" : "s") << "\n";
    }
}

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
    KW_NAMESPACE,
    KW_USING,
    KW_PUB,
    KW_OWN,
    KW_NEW,
    KW_DROP,
    KW_MUT,
    KW_SOME,
    KW_NONE,
    KW_SWITCH,
    KW_CASE,
    KW_DEFAULT,
    KW_CONST,
    KW_SIZEOF,

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
    LESS_LESS,
    GREATER,
    GREATER_EQUAL,
    GREATER_GREATER,
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
    QUESTION,
    SEMICOLON,
    COLON,
    COLON_COLON,
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
    std::string file;

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
        case TokenType::KW_NAMESPACE:   return "KW_NAMESPACE";
        case TokenType::KW_USING:       return "KW_USING";
        case TokenType::KW_PUB:         return "KW_PUB";
        case TokenType::KW_OWN:         return "KW_OWN";
        case TokenType::KW_NEW:         return "KW_NEW";
        case TokenType::KW_DROP:        return "KW_DROP";
        case TokenType::KW_MUT:         return "KW_MUT";
        case TokenType::KW_SOME:        return "KW_SOME";
        case TokenType::KW_NONE:        return "KW_NONE";
        case TokenType::KW_SWITCH:      return "KW_SWITCH";
        case TokenType::KW_CASE:        return "KW_CASE";
        case TokenType::KW_DEFAULT:     return "KW_DEFAULT";
        case TokenType::KW_CONST:       return "KW_CONST";
        case TokenType::KW_SIZEOF:      return "KW_SIZEOF";

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
        case TokenType::LESS_LESS:      return "LESS_LESS";
        case TokenType::GREATER:        return "GREATER";
        case TokenType::GREATER_EQUAL:  return "GREATER_EQUAL";
        case TokenType::GREATER_GREATER: return "GREATER_GREATER";
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
        case TokenType::QUESTION:       return "QUESTION";
        case TokenType::COLON:          return "COLON";
        case TokenType::COLON_COLON:    return "COLON_COLON";
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
    {"namespace", TokenType::KW_NAMESPACE},
    {"using", TokenType::KW_USING},
    {"pub", TokenType::KW_PUB},
    {"own", TokenType::KW_OWN},
    {"new", TokenType::KW_NEW},
    {"drop", TokenType::KW_DROP},
    {"mut", TokenType::KW_MUT},
    {"some", TokenType::KW_SOME},
    {"none", TokenType::KW_NONE},
    {"switch", TokenType::KW_SWITCH},
    {"case", TokenType::KW_CASE},
    {"default", TokenType::KW_DEFAULT},
    {"const", TokenType::KW_CONST},
    {"sizeof", TokenType::KW_SIZEOF},
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
            case ':':
                if (peek() == ':') {
                    advance();
                    tokens.push_back(makeToken(TokenType::COLON_COLON, "::"));
                } else {
                    tokens.push_back(makeToken(TokenType::COLON, ":"));
                }
                break;
            case ',': tokens.push_back(makeToken(TokenType::COMMA, ",")); break;
            case '.': tokens.push_back(makeToken(TokenType::DOT, ".")); break;
            case '~': tokens.push_back(makeToken(TokenType::TILDE, "~")); break;
            case '?': tokens.push_back(makeToken(TokenType::QUESTION, "?")); break;

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
                } else if (peek() == '<') {
                    advance();
                    tokens.push_back(makeToken(TokenType::LESS_LESS, "<<"));
                } else {
                    tokens.push_back(makeToken(TokenType::LESS, "<"));
                }
                break;

            case '>':
                if (peek() == '=') {
                    advance();
                    tokens.push_back(makeToken(TokenType::GREATER_EQUAL, ">="));
                } else if (peek() == '>') {
                    advance();
                    tokens.push_back(makeToken(TokenType::GREATER_GREATER, ">>"));
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

struct SourceLocation {
    std::string file;
    int line = 0;
    int column = 0;

    bool known() const { return line > 0; }
};

class ASTNode {
public:
    SourceLocation loc;

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

enum class Ownership {
    Value,
    Owned,
    SharedBorrow,
    MutBorrow,
};

struct Type {
    PrimitiveType base;
    int pointerLevel = 0;
    std::string structName;
    std::string funcPointerTypedefName;
    std::string enumName;
    Ownership ownership = Ownership::Value;
    bool optional = false;
    bool slice = false;
    bool ownedElements = false;
    int fixedLength = -1;

    bool isOwned() const { return ownership == Ownership::Owned; }
    bool isSharedBorrow() const { return ownership == Ownership::SharedBorrow; }
    bool isMutBorrow() const { return ownership == Ownership::MutBorrow; }
    bool isBorrow() const { return isSharedBorrow() || isMutBorrow(); }
    bool isVoid() const { return base == PrimitiveType::VOID && pointerLevel == 0; }
    bool isStruct() const { return !structName.empty(); }
    bool isFunctionPointer() const { return !funcPointerTypedefName.empty(); }
    bool isEnum() const { return !enumName.empty() && pointerLevel == 0; }
};

std::string typeToString(const Type& type);

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
        NONE,
        INTEGER,
        FLOAT,
        STRING,
        BOOLEAN,
    };

    Kind kind;
    std::string value;
    std::string enumName;

    Literal(Kind kind, const std::string& value, const std::string& enumName = "")
        : kind(kind), value(value), enumName(enumName) {}

    void accept(ASTVisitor* visitor) override;
};

class NewSliceExpr : public Expression {
public:
    Type type;
    std::unique_ptr<Expression> count;

    NewSliceExpr(const Type& type, std::unique_ptr<Expression> count)
        : type(type), count(std::move(count)) {}

    void accept(ASTVisitor* visitor) override;
};

class NewExpr : public Expression {
public:
    Type type;
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> fields;

    NewExpr(const Type& type,
            std::vector<std::pair<std::string, std::unique_ptr<Expression>>> fields)
        : type(type), fields(std::move(fields)) {}

    void accept(ASTVisitor* visitor) override;
};

class SizeofExpr : public Expression {
public:
    Type targetType;

    explicit SizeofExpr(const Type& targetType)
        : targetType(targetType) {}

    void accept(ASTVisitor* visitor) override;
};

class Identifier : public Expression {
public:
    std::string name;
    bool isMoveSource = false;

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
        SHIFT_LEFT,
        SHIFT_RIGHT,
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
    bool mutableBorrow = false;
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

    std::unique_ptr<Expression> callee;
    std::vector<std::unique_ptr<Expression>> arguments;

    FunctionCall(const std::string& functionName,
                 std::vector<std::unique_ptr<Expression>> arguments)
        : functionName(functionName), arguments(std::move(arguments)) {}

    FunctionCall(std::unique_ptr<Expression> callee,
                 std::vector<std::unique_ptr<Expression>> arguments)
        : callee(std::move(callee)), arguments(std::move(arguments)) {}

    bool isIndirect() const { return callee != nullptr; }

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
    int arraySize;

    VariableDecl(const Type& type, const std::string& name,
                 std::unique_ptr<Expression> initializer = nullptr,
                 bool isConst = false, int arraySize = 0)
        : type(type), name(name), initializer(std::move(initializer)),
          isConst(isConst), arraySize(arraySize) {}

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

class IfSomeStmt : public Statement {
public:
    std::string binding;
    bool mutableBinding = false;
    std::unique_ptr<Expression> source;
    std::unique_ptr<Statement> thenBranch;
    std::unique_ptr<Statement> elseBranch;

    IfSomeStmt(const std::string& binding, bool mutableBinding,
               std::unique_ptr<Expression> source,
               std::unique_ptr<Statement> thenBranch,
               std::unique_ptr<Statement> elseBranch)
        : binding(binding), mutableBinding(mutableBinding), source(std::move(source)),
          thenBranch(std::move(thenBranch)), elseBranch(std::move(elseBranch)) {}

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

    bool isPublic = false;
    std::string dropsType;

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

struct EnumConstant {
    std::string name;
    int value;
};

struct EnumDecl {
    std::string name;
    std::vector<EnumConstant> constants;
};

struct GlobalVariable {
    Type type;
    std::string name;
    std::unique_ptr<Expression> initializer;
    bool isConst;
    bool isPublic = false;
    SourceLocation loc;

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
    std::vector<EnumDecl> enums;

    Program(std::vector<std::unique_ptr<StructDecl>> structs = {},
            std::vector<std::unique_ptr<FunctionDecl>> functions = {},
            std::vector<FuncPointerTypedef> funcPointerTypedefs = {},
            std::vector<GlobalVariable> globalVariables = {},
            std::vector<EnumDecl> enums = {})
        : structs(std::move(structs)), functions(std::move(functions)),
          funcPointerTypedefs(std::move(funcPointerTypedefs)),
          globalVariables(std::move(globalVariables)),
          enums(std::move(enums)) {}

    void accept(ASTVisitor* visitor) override;
};

class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;

    virtual void visit(Literal* node) = 0;
    virtual void visit(SizeofExpr* node) = 0;
    virtual void visit(NewExpr* node) = 0;
    virtual void visit(NewSliceExpr* node) = 0;
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
    virtual void visit(IfSomeStmt* node) = 0;
    virtual void visit(ForStmt* node) = 0;
    virtual void visit(WhileStmt* node) = 0;
    virtual void visit(BreakStmt* node) = 0;
    virtual void visit(ContinueStmt* node) = 0;
    virtual void visit(SwitchStmt* node) = 0;
    virtual void visit(StructDecl* node) = 0;
    virtual void visit(FunctionDecl* node) = 0;
    virtual void visit(Program* node) = 0;
};

std::string typeToString(const Type& type) {
    std::string name;

    if (type.isFunctionPointer()) {
        name = type.funcPointerTypedefName;
    } else if (!type.enumName.empty()) {
        name = type.enumName;
    } else if (type.isStruct()) {
        name = type.structName;
    } else if (type.base == PrimitiveType::CHAR && type.pointerLevel == 1) {
        return "string";
    } else {
        switch (type.base) {
            case PrimitiveType::INT:    name = "int"; break;
            case PrimitiveType::FLOAT:  name = "float"; break;
            case PrimitiveType::DOUBLE: name = "double"; break;
            case PrimitiveType::BOOL:   name = "bool"; break;
            case PrimitiveType::CHAR:   name = "char"; break;
            case PrimitiveType::VOID:   name = "void"; break;
            default:                    name = "unknown"; break;
        }
    }

    if (type.slice) {
        std::string inner = type.ownedElements ? "own " + name : name;
        if (type.fixedLength >= 0) {
            name = "[" + inner + "; " + std::to_string(type.fixedLength) + "]";
        } else {
            name = "[" + inner + "]";
        }
    }
    std::string suffix = type.optional ? "?" : "";
    if (type.isOwned()) {
        return "own " + name + suffix;
    }
    if (type.isSharedBorrow()) {
        return "&" + name + suffix;
    }
    if (type.isMutBorrow()) {
        return "&mut " + name + suffix;
    }
    for (int i = 0; i < type.pointerLevel; ++i) {
        name += "*";
    }
    return name;
}

void Literal::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

void NewExpr::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

void NewSliceExpr::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}

void SizeofExpr::accept(ASTVisitor* visitor) {
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

void IfSomeStmt::accept(ASTVisitor* visitor) {
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
    struct GenericTemplate {
        std::string name;
        std::vector<std::string> typeParams;
        std::vector<b::lexer::Token> tokens;
        size_t nameIndex = 0;
        bool isStruct = false;
    };

    struct PendingInstantiation {
        std::string templateName;
        std::string mangledName;
        std::vector<b::ast::Type> typeArgs;
        bool isStruct = false;
    };

    std::vector<b::lexer::Token> tokens;
    size_t current;
    std::unordered_map<std::string, int> enumConstants;
    std::unordered_map<std::string, std::string> enumConstantOwner;
    std::unordered_set<std::string> enumTypeNames;
    std::vector<b::ast::EnumDecl> enumDecls;
    std::unordered_set<std::string> funcPointerTypedefNames;
    std::unordered_set<std::string> constVariables;
    std::unordered_map<std::string, int> constantLengths;
    std::unordered_map<std::string, GenericTemplate> genericStructs;
    std::unordered_map<std::string, GenericTemplate> genericFunctions;
    std::vector<PendingInstantiation> pendingInstantiations;
    std::unordered_set<std::string> requestedInstantiations;

    void prescanDeclarations();
    size_t scanEnumDeclaration(size_t index);
    void scanTypedefName(size_t index);
    bool scanGenericTemplate(size_t declStart, size_t nameIndex, bool isStruct, size_t& outNext);
    bool isGenericHeader(size_t nameIndex, bool requireCallParen) const;

    std::string mangleTypeName(const b::ast::Type& type) const;
    std::vector<b::lexer::Token> typeToTokens(const b::ast::Type& type) const;
    std::vector<b::ast::Type> parseGenericArgList();
    void consumeGenericClose();
    std::string requestInstantiation(const std::string& templateName, bool isStruct,
                                     const std::vector<b::ast::Type>& typeArgs);
    std::vector<b::lexer::Token> instantiateTemplate(const GenericTemplate& tmpl,
                                                     const PendingInstantiation& job) const;
    bool looksLikeDeclarationStart() const;
    std::string location() const;

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
    std::unique_ptr<b::ast::FunctionDecl> parseDropDecl();
    std::unique_ptr<b::ast::Statement> parseStatement();
    std::unique_ptr<b::ast::Statement> parseBlock();
    std::unique_ptr<b::ast::Statement> parseVariableDecl();
    std::unique_ptr<b::ast::Statement> parseIfStmt();
    std::unique_ptr<b::ast::Statement> parseIfSomeStmt();
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
    std::unique_ptr<b::ast::Expression> parseShift();
    std::unique_ptr<b::ast::Expression> parseAdditive();
    std::unique_ptr<b::ast::Expression> parseMultiplicative();
    std::unique_ptr<b::ast::Expression> parseUnary();
    std::unique_ptr<b::ast::Expression> parsePostfix();
    std::unique_ptr<b::ast::Expression> parsePrimary();

    std::vector<b::ast::Parameter> parseParameterList();
    b::ast::Type parseType();
    int parseConstantLength();

    b::ast::SourceLocation locationAt(size_t index) const;

    template <typename T, typename... Args>
    std::unique_ptr<T> makeNode(size_t startToken, Args&&... args) {
        auto node = std::make_unique<T>(std::forward<Args>(args)...);
        node->loc = locationAt(startToken);
        return node;
    }
};

Parser::Parser(const std::vector<b::lexer::Token>& tokens)
    : tokens(tokens), current(0) {}

std::unique_ptr<b::ast::Program> Parser::parse() {
    size_t nodeStart = current;
    std::vector<std::unique_ptr<b::ast::StructDecl>> structs;
    std::vector<std::unique_ptr<b::ast::FunctionDecl>> functions;
    std::vector<b::ast::FuncPointerTypedef> typedefs;
    std::vector<b::ast::GlobalVariable> globals;

    prescanDeclarations();

    while (!isAtEnd()) {
        if (check(b::lexer::TokenType::EOF_TOKEN)) break;
        nodeStart = current;
        if (check(b::lexer::TokenType::KW_IMPORT)) {
            advance();
            consume(b::lexer::TokenType::STRING, "Expected module path after 'import'");
            consume(b::lexer::TokenType::SEMICOLON, "Expected ';' after import");
        } else if (check(b::lexer::TokenType::KW_DROP)) {
            functions.push_back(parseDropDecl());
        } else {

            bool isPublic = match(b::lexer::TokenType::KW_PUB);

            if (check(b::lexer::TokenType::KW_STRUCT)) {
                structs.push_back(parseStructDecl());
            } else if (check(b::lexer::TokenType::KW_ENUM)) {
                parseEnumDecl();
            } else if (check(b::lexer::TokenType::KW_TYPEDEF)) {
                typedefs.push_back(parseTypedefDecl());
            } else if (check(b::lexer::TokenType::KW_CONST)) {
                advance();
                b::ast::Type type = parseType();
                std::string name = consume(b::lexer::TokenType::IDENTIFIER, "Expected identifier" + location()).lexeme;
                consume(b::lexer::TokenType::EQUAL, "Expected '=' in global const declaration" + location());
                auto init = parseExpression();
                consume(b::lexer::TokenType::SEMICOLON, "Expected ';' after global const" + location());
                globals.emplace_back(type, name, std::move(init), true);
                globals.back().isPublic = isPublic;
                globals.back().loc = locationAt(nodeStart);
                constVariables.insert(name);
                if (auto* literal =
                        dynamic_cast<b::ast::Literal*>(globals.back().initializer.get())) {
                    if (literal->kind == b::ast::Literal::Kind::INTEGER) {
                        try {
                            long long value = std::stoll(literal->value);
                            if (value >= 0 && value <= 2147483647LL) {
                                constantLengths[name] = static_cast<int>(value);
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
            } else {
                size_t saveTypePos = current;
                b::ast::Type type = parseType();
                if (!check(b::lexer::TokenType::IDENTIFIER)) {
                    throw ParseException("Expected name after type in top-level declaration" + location());
                }
                std::string name = advance().lexeme;
                if (check(b::lexer::TokenType::LPAREN)) {
                    current = saveTypePos;
                    functions.push_back(parseFunctionDecl());
                    functions.back()->isPublic = isPublic;
                } else {
                    if (check(b::lexer::TokenType::LBRACKET)) {
                        throw ParseException("Fixed-size arrays are only supported for local variables; "
                                             "allocate global buffers with malloc" + location());
                    }
                    std::unique_ptr<b::ast::Expression> init = nullptr;
                    if (match(b::lexer::TokenType::EQUAL)) {
                        init = parseExpression();
                    }
                    consume(b::lexer::TokenType::SEMICOLON, "Expected ';' after global variable" + location());
                    globals.emplace_back(type, name, std::move(init), false);
                    globals.back().isPublic = isPublic;
                    globals.back().loc = locationAt(nodeStart);
                }
            }
        }
    }

    while (!pendingInstantiations.empty()) {
        PendingInstantiation job = pendingInstantiations.front();
        pendingInstantiations.erase(pendingInstantiations.begin());

        const auto& templates = job.isStruct ? genericStructs : genericFunctions;
        auto it = templates.find(job.templateName);
        if (it == templates.end()) {
            throw ParseException("Unknown generic template: " + job.templateName);
        }

        std::vector<b::lexer::Token> savedTokens = std::move(tokens);
        size_t savedCurrent = current;

        tokens = instantiateTemplate(it->second, job);
        tokens.push_back(b::lexer::Token(b::lexer::TokenType::EOF_TOKEN, "", "", 0, 0));
        current = 0;

        if (job.isStruct) {
            structs.push_back(parseStructDecl());
        } else {
            functions.push_back(parseFunctionDecl());
        }

        tokens = std::move(savedTokens);
        current = savedCurrent;
    }

    return makeNode<b::ast::Program>(nodeStart, std::move(structs), std::move(functions),
                                             std::move(typedefs), std::move(globals),
                                             std::move(enumDecls));
}

b::ast::SourceLocation Parser::locationAt(size_t index) const {
    b::ast::SourceLocation where;
    if (index < tokens.size()) {
        where.file = tokens[index].file;
        where.line = tokens[index].line;
        where.column = tokens[index].column;
    }
    return where;
}

std::string Parser::location() const {
    if (current >= tokens.size()) {
        return "";
    }
    const b::lexer::Token& token = tokens[current];
    std::string where = " (";
    if (!token.file.empty()) {
        where += token.file + ":";
    }
    where += "line " + std::to_string(token.line) + ")";
    return where;
}

void Parser::prescanDeclarations() {
    std::vector<b::lexer::Token> filtered;
    size_t i = 0;
    int depth = 0;
    size_t declStartInFiltered = 0;
    size_t declStartInSource = 0;

    while (i < tokens.size()) {
        const b::lexer::Token& token = tokens[i];

        if (token.type == b::lexer::TokenType::EOF_TOKEN) {
            filtered.push_back(token);
            ++i;
            continue;
        }

        if (depth == 0 && token.type == b::lexer::TokenType::KW_ENUM) {
            size_t next = scanEnumDeclaration(i);
            for (size_t k = i; k < next; ++k) {
                filtered.push_back(tokens[k]);
            }
            i = next;
            declStartInFiltered = filtered.size();
            declStartInSource = i;
            continue;
        }

        if (depth == 0 && token.type == b::lexer::TokenType::KW_TYPEDEF) {
            scanTypedefName(i);
        }

        size_t structAt = i;
        if (depth == 0 && token.type == b::lexer::TokenType::KW_PUB && i + 1 < tokens.size() &&
            tokens[i + 1].type == b::lexer::TokenType::KW_STRUCT) {
            structAt = i + 1;
        }
        if (depth == 0 && tokens[structAt].type == b::lexer::TokenType::KW_STRUCT &&
            structAt + 2 < tokens.size() &&
            tokens[structAt + 1].type == b::lexer::TokenType::IDENTIFIER &&
            tokens[structAt + 2].type == b::lexer::TokenType::LESS) {
            size_t next = 0;
            if (scanGenericTemplate(structAt, structAt + 1, true, next)) {
                i = next;
                declStartInFiltered = filtered.size();
                declStartInSource = i;
                continue;
            }
        }

        if (depth == 0 && token.type == b::lexer::TokenType::IDENTIFIER &&
            i > declStartInSource && isGenericHeader(i, true)) {
            size_t next = 0;
            if (scanGenericTemplate(declStartInSource, i, false, next)) {
                filtered.resize(declStartInFiltered);
                i = next;
                declStartInSource = i;
                continue;
            }
        }

        if (token.type == b::lexer::TokenType::LBRACE) {
            depth++;
        } else if (token.type == b::lexer::TokenType::RBRACE) {
            depth--;
        }

        filtered.push_back(token);
        ++i;

        if (depth == 0 &&
            (token.type == b::lexer::TokenType::SEMICOLON ||
             token.type == b::lexer::TokenType::RBRACE)) {
            declStartInFiltered = filtered.size();
            declStartInSource = i;
        }
    }

    tokens = std::move(filtered);
    current = 0;
}

size_t Parser::scanEnumDeclaration(size_t index) {
    size_t i = index + 1;
    if (i >= tokens.size() || tokens[i].type != b::lexer::TokenType::IDENTIFIER) {
        throw ParseException("Expected enum name");
    }

    b::ast::EnumDecl decl;
    decl.name = tokens[i].lexeme;

    if (enumTypeNames.count(decl.name)) {
        throw ParseException("Duplicate enum type: " + decl.name);
    }
    enumTypeNames.insert(decl.name);
    ++i;

    if (i >= tokens.size() || tokens[i].type != b::lexer::TokenType::LBRACE) {
        throw ParseException("Expected '{' after enum name " + decl.name);
    }
    ++i;

    int nextValue = 0;
    while (i < tokens.size() && tokens[i].type != b::lexer::TokenType::RBRACE) {
        if (tokens[i].type != b::lexer::TokenType::IDENTIFIER) {
            throw ParseException("Expected enum constant name in enum " + decl.name);
        }
        std::string constantName = tokens[i].lexeme;
        ++i;

        int value = nextValue;
        if (i < tokens.size() && tokens[i].type == b::lexer::TokenType::EQUAL) {
            ++i;
            bool negative = false;
            if (i < tokens.size() && tokens[i].type == b::lexer::TokenType::MINUS) {
                negative = true;
                ++i;
            }
            if (i >= tokens.size() || tokens[i].type != b::lexer::TokenType::INTEGER) {
                throw ParseException("Expected integer after '=' in enum " + decl.name);
            }
            value = std::stoi(tokens[i].value);
            if (negative) {
                value = -value;
            }
            ++i;
        }

        auto existing = enumConstantOwner.find(constantName);
        if (existing != enumConstantOwner.end()) {
            throw ParseException("Enum constant '" + constantName + "' is already defined in enum " +
                                 existing->second);
        }

        enumConstants[constantName] = value;
        enumConstantOwner[constantName] = decl.name;
        decl.constants.push_back({constantName, value});
        nextValue = value + 1;

        if (i < tokens.size() && tokens[i].type == b::lexer::TokenType::COMMA) {
            ++i;
        } else {
            break;
        }
    }

    if (i >= tokens.size() || tokens[i].type != b::lexer::TokenType::RBRACE) {
        throw ParseException("Expected '}' after enum " + decl.name);
    }
    ++i;

    if (i >= tokens.size() || tokens[i].type != b::lexer::TokenType::SEMICOLON) {
        throw ParseException("Expected ';' after enum " + decl.name);
    }
    ++i;

    enumDecls.push_back(std::move(decl));
    return i;
}

void Parser::scanTypedefName(size_t index) {
    for (size_t i = index; i + 2 < tokens.size(); ++i) {
        if (tokens[i].type == b::lexer::TokenType::SEMICOLON) {
            return;
        }
        if (tokens[i].type == b::lexer::TokenType::LPAREN &&
            tokens[i + 1].type == b::lexer::TokenType::STAR &&
            tokens[i + 2].type == b::lexer::TokenType::IDENTIFIER) {
            funcPointerTypedefNames.insert(tokens[i + 2].lexeme);
            return;
        }
    }
}

bool Parser::isGenericHeader(size_t nameIndex, bool requireCallParen) const {
    size_t i = nameIndex + 1;
    if (i >= tokens.size() || tokens[i].type != b::lexer::TokenType::LESS) {
        return false;
    }
    ++i;

    bool expectName = true;
    while (i < tokens.size()) {
        if (expectName) {
            if (tokens[i].type != b::lexer::TokenType::IDENTIFIER) {
                return false;
            }
            expectName = false;
        } else if (tokens[i].type == b::lexer::TokenType::COMMA) {
            expectName = true;
        } else if (tokens[i].type == b::lexer::TokenType::GREATER) {
            if (!requireCallParen) {
                return true;
            }
            return i + 1 < tokens.size() && tokens[i + 1].type == b::lexer::TokenType::LPAREN;
        } else {
            return false;
        }
        ++i;
    }
    return false;
}

bool Parser::scanGenericTemplate(size_t declStart, size_t nameIndex, bool isStruct, size_t& outNext) {
    if (!isGenericHeader(nameIndex, false)) {
        return false;
    }

    GenericTemplate tmpl;
    tmpl.name = tokens[nameIndex].lexeme;
    tmpl.isStruct = isStruct;
    tmpl.nameIndex = nameIndex - declStart;

    size_t i = nameIndex + 2;
    while (i < tokens.size() && tokens[i].type != b::lexer::TokenType::GREATER) {
        if (tokens[i].type == b::lexer::TokenType::IDENTIFIER) {
            tmpl.typeParams.push_back(tokens[i].lexeme);
        }
        ++i;
    }
    if (i >= tokens.size()) {
        return false;
    }
    size_t afterParams = i + 1;

    if (tmpl.typeParams.empty()) {
        throw ParseException("Generic declaration '" + tmpl.name + "' needs at least one type parameter");
    }

    size_t end = 0;
    if (isStruct) {
        if (afterParams >= tokens.size() || tokens[afterParams].type != b::lexer::TokenType::LBRACE) {
            return false;
        }
        int depth = 0;
        size_t k = afterParams;
        for (; k < tokens.size(); ++k) {
            if (tokens[k].type == b::lexer::TokenType::LBRACE) depth++;
            else if (tokens[k].type == b::lexer::TokenType::RBRACE) {
                depth--;
                if (depth == 0) break;
            }
        }
        if (k >= tokens.size()) {
            throw ParseException("Unterminated generic struct " + tmpl.name);
        }
        if (k + 1 >= tokens.size() || tokens[k + 1].type != b::lexer::TokenType::SEMICOLON) {
            throw ParseException("Expected ';' after generic struct " + tmpl.name);
        }
        end = k + 1;
    } else {
        if (afterParams >= tokens.size() || tokens[afterParams].type != b::lexer::TokenType::LPAREN) {
            return false;
        }
        int depth = 0;
        size_t k = afterParams;
        for (; k < tokens.size(); ++k) {
            if (tokens[k].type == b::lexer::TokenType::LPAREN) depth++;
            else if (tokens[k].type == b::lexer::TokenType::RPAREN) {
                depth--;
                if (depth == 0) break;
            }
        }
        if (k >= tokens.size() || k + 1 >= tokens.size() ||
            tokens[k + 1].type != b::lexer::TokenType::LBRACE) {
            throw ParseException("Expected function body for generic function " + tmpl.name);
        }
        depth = 0;
        size_t b = k + 1;
        for (; b < tokens.size(); ++b) {
            if (tokens[b].type == b::lexer::TokenType::LBRACE) depth++;
            else if (tokens[b].type == b::lexer::TokenType::RBRACE) {
                depth--;
                if (depth == 0) break;
            }
        }
        if (b >= tokens.size()) {
            throw ParseException("Unterminated generic function " + tmpl.name);
        }
        end = b;
    }

    for (size_t k = declStart; k <= nameIndex; ++k) {
        tmpl.tokens.push_back(tokens[k]);
    }
    for (size_t k = afterParams; k <= end; ++k) {
        tmpl.tokens.push_back(tokens[k]);
    }

    auto& target = isStruct ? genericStructs : genericFunctions;
    if (target.count(tmpl.name)) {
        throw ParseException("Duplicate generic declaration: " + tmpl.name);
    }
    target[tmpl.name] = std::move(tmpl);

    outNext = end + 1;
    return true;
}

std::string Parser::mangleTypeName(const b::ast::Type& type) const {
    std::string name;

    if (type.isFunctionPointer()) {
        name = type.funcPointerTypedefName;
    } else if (!type.enumName.empty()) {
        name = type.enumName;
    } else if (type.isStruct()) {
        name = type.structName;
    } else {
        switch (type.base) {
            case b::ast::PrimitiveType::INT:    name = "int"; break;
            case b::ast::PrimitiveType::FLOAT:  name = "float"; break;
            case b::ast::PrimitiveType::DOUBLE: name = "double"; break;
            case b::ast::PrimitiveType::BOOL:   name = "bool"; break;
            case b::ast::PrimitiveType::CHAR:   name = "char"; break;
            case b::ast::PrimitiveType::VOID:   name = "void"; break;
            default:                            name = "unknown"; break;
        }
    }

    for (int i = 0; i < type.pointerLevel; ++i) {
        name += "_ptr";
    }
    return name;
}

std::vector<b::lexer::Token> Parser::typeToTokens(const b::ast::Type& type) const {
    std::vector<b::lexer::Token> result;
    b::lexer::TokenType baseType = b::lexer::TokenType::IDENTIFIER;
    std::string lexeme;

    if (type.ownership == b::ast::Ownership::Owned) {
        result.push_back(b::lexer::Token(b::lexer::TokenType::KW_OWN, "own", "own", 0, 0));
        b::ast::Type bare = type;
        bare.ownership = b::ast::Ownership::Value;
        bare.optional = false;
        bare.pointerLevel = 0;
        for (auto& token : typeToTokens(bare)) {
            result.push_back(token);
        }
        if (type.optional) {
            result.push_back(b::lexer::Token(b::lexer::TokenType::QUESTION, "?", "?", 0, 0));
        }
        return result;
    }

    if (type.base == b::ast::PrimitiveType::CHAR && type.pointerLevel == 1 && !type.isStruct() &&
        type.ownership == b::ast::Ownership::Value) {
        result.push_back(b::lexer::Token(b::lexer::TokenType::KW_STRING, "string", "string", 0, 0));
        return result;
    }

    if (type.isFunctionPointer()) {
        lexeme = type.funcPointerTypedefName;
    } else if (!type.enumName.empty()) {
        lexeme = type.enumName;
    } else if (type.isStruct()) {
        lexeme = type.structName;
    } else {
        switch (type.base) {
            case b::ast::PrimitiveType::INT:
                baseType = b::lexer::TokenType::KW_INT; lexeme = "int"; break;
            case b::ast::PrimitiveType::FLOAT:
                baseType = b::lexer::TokenType::KW_FLOAT; lexeme = "float"; break;
            case b::ast::PrimitiveType::DOUBLE:
                baseType = b::lexer::TokenType::KW_DOUBLE; lexeme = "double"; break;
            case b::ast::PrimitiveType::BOOL:
                baseType = b::lexer::TokenType::KW_BOOL; lexeme = "bool"; break;
            case b::ast::PrimitiveType::CHAR:
                baseType = b::lexer::TokenType::KW_CHAR; lexeme = "char"; break;
            case b::ast::PrimitiveType::VOID:
                baseType = b::lexer::TokenType::KW_VOID; lexeme = "void"; break;
            default:
                throw ParseException("Cannot use this type as a generic argument");
        }
    }

    result.push_back(b::lexer::Token(baseType, lexeme, lexeme, 0, 0));
    if (type.pointerLevel > 0) {
        throw ParseException("Cannot use a raw pointer type as a generic argument");
    }
    return result;
}

void Parser::consumeGenericClose() {
    if (check(b::lexer::TokenType::GREATER)) {
        advance();
        return;
    }
    if (check(b::lexer::TokenType::GREATER_GREATER)) {
        tokens[current] = b::lexer::Token(b::lexer::TokenType::GREATER, ">", ">",
                                          tokens[current].line, tokens[current].column + 1);
        return;
    }
    throw ParseException("Expected '>' after generic type arguments" + location());
}

std::vector<b::ast::Type> Parser::parseGenericArgList() {
    consume(b::lexer::TokenType::LESS, "Expected '<' before generic type arguments" + location());

    std::vector<b::ast::Type> args;
    do {
        args.push_back(parseType());
    } while (match(b::lexer::TokenType::COMMA));

    consumeGenericClose();
    return args;
}

std::string Parser::requestInstantiation(const std::string& templateName, bool isStruct,
                                         const std::vector<b::ast::Type>& typeArgs) {
    const auto& templates = isStruct ? genericStructs : genericFunctions;
    auto it = templates.find(templateName);
    if (it == templates.end()) {
        throw ParseException("Unknown generic: " + templateName);
    }

    if (it->second.typeParams.size() != typeArgs.size()) {
        throw ParseException(templateName + " expects " +
                             std::to_string(it->second.typeParams.size()) +
                             " type argument(s), got " + std::to_string(typeArgs.size()) + location());
    }

    std::string mangled = templateName;
    for (const auto& arg : typeArgs) {
        mangled += "__" + mangleTypeName(arg);
    }

    if (!requestedInstantiations.count(mangled)) {
        requestedInstantiations.insert(mangled);
        PendingInstantiation job;
        job.templateName = templateName;
        job.mangledName = mangled;
        job.typeArgs = typeArgs;
        job.isStruct = isStruct;
        pendingInstantiations.push_back(std::move(job));
    }

    return mangled;
}

std::vector<b::lexer::Token> Parser::instantiateTemplate(const GenericTemplate& tmpl,
                                                         const PendingInstantiation& job) const {
    std::unordered_map<std::string, std::vector<b::lexer::Token>> substitutions;
    for (size_t i = 0; i < tmpl.typeParams.size(); ++i) {
        substitutions[tmpl.typeParams[i]] = typeToTokens(job.typeArgs[i]);
    }

    std::vector<b::lexer::Token> result;
    for (size_t i = 0; i < tmpl.tokens.size(); ++i) {
        const b::lexer::Token& token = tmpl.tokens[i];

        if (i == tmpl.nameIndex) {
            b::lexer::Token renamed = token;
            renamed.lexeme = job.mangledName;
            renamed.value = job.mangledName;
            result.push_back(renamed);
            continue;
        }

        if (token.type == b::lexer::TokenType::IDENTIFIER) {
            auto it = substitutions.find(token.lexeme);
            if (it != substitutions.end()) {
                for (b::lexer::Token replacement : it->second) {
                    replacement.line = token.line;
                    replacement.column = token.column;
                    replacement.file = token.file;
                    result.push_back(replacement);
                }
                continue;
            }
        }

        result.push_back(token);
    }

    return result;
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

int Parser::parseConstantLength() {
    if (check(b::lexer::TokenType::INTEGER)) {
        long long value = 0;
        try {
            value = std::stoll(advance().value);
        } catch (const std::exception&) {
            throw ParseException("That is not a valid slice length" + location());
        }
        if (value < 0 || value > 2147483647LL) {
            throw ParseException("A slice length must fit in 'int' and cannot be negative" +
                                 location());
        }
        return static_cast<int>(value);
    }
    if (check(b::lexer::TokenType::IDENTIFIER)) {
        std::string name = peek().lexeme;
        auto known = constantLengths.find(name);
        if (known != constantLengths.end()) {
            advance();
            return known->second;
        }
        throw ParseException("'" + name +
                             "' is not a compile-time constant, so it cannot be a slice length" +
                             location());
    }
    throw ParseException("Expected a constant length after ';'" + location());
}

b::ast::Type Parser::parseType() {
    b::ast::Type type;
    type.pointerLevel = 0;

    bool owning = match(b::lexer::TokenType::KW_OWN);
    bool borrowing = false;
    bool borrowingMutably = false;
    if (!owning && match(b::lexer::TokenType::AMPERSAND)) {
        borrowing = true;
        borrowingMutably = match(b::lexer::TokenType::KW_MUT);
    }

    if ((owning || borrowing) && match(b::lexer::TokenType::LBRACKET)) {
        b::ast::Type element = parseType();
        int declaredLength = -1;
        if (match(b::lexer::TokenType::SEMICOLON)) {
            declaredLength = parseConstantLength();
        }
        consume(b::lexer::TokenType::RBRACKET, "Expected ']' after the element type" + location());
        if (element.slice || element.isBorrow()) {
            throw ParseException("A slice holds values or owned handles, not '" +
                                 b::ast::typeToString(element) + "'" + location());
        }
        bool holdsOwned = element.isOwned();
        element.slice = true;
        element.ownedElements = holdsOwned;
        element.fixedLength = declaredLength;
        element.pointerLevel = 1;
        element.ownership = b::ast::Ownership::Value;
        element.optional = false;
        element.ownership = owning ? b::ast::Ownership::Owned
                                   : (borrowingMutably ? b::ast::Ownership::MutBorrow
                                                       : b::ast::Ownership::SharedBorrow);
        if (match(b::lexer::TokenType::QUESTION)) {
            element.optional = true;
        }
        return element;
    }

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
               genericStructs.count(peek().lexeme) > 0 &&
               current + 1 < tokens.size() &&
               tokens[current + 1].type == b::lexer::TokenType::LESS) {
        std::string templateName = advance().lexeme;
        std::vector<b::ast::Type> typeArgs = parseGenericArgList();
        type.structName = requestInstantiation(templateName, true, typeArgs);
        type.base = b::ast::PrimitiveType::INT;
    } else if (check(b::lexer::TokenType::IDENTIFIER) &&
               enumTypeNames.count(peek().lexeme) > 0) {
        type.enumName = advance().lexeme;
        type.base = b::ast::PrimitiveType::INT;
    } else if (check(b::lexer::TokenType::IDENTIFIER) &&
               funcPointerTypedefNames.count(peek().lexeme) > 0) {
        type.funcPointerTypedefName = advance().lexeme;
        type.base = b::ast::PrimitiveType::INT;
    } else if (check(b::lexer::TokenType::IDENTIFIER)) {
        if (genericStructs.count(peek().lexeme) > 0) {
            throw ParseException("Generic struct '" + peek().lexeme +
                                 "' needs type arguments, for example " + peek().lexeme + "<int>" + location());
        }
        type.structName = advance().lexeme;
        type.base = b::ast::PrimitiveType::INT;
    } else {
        throw ParseException("Expected type, got '" + peek().lexeme + "'" + location());
    }

    if (check(b::lexer::TokenType::STAR)) {
        throw ParseException("Raw pointers were removed from B; write 'own " +
                             b::ast::typeToString(type) + "' to own a value, '&" +
                             b::ast::typeToString(type) + "' to borrow it, or add '?' to allow none" +
                             location());
    }

    if (owning) {
        if (type.pointerLevel != 0) {
            throw ParseException("'own' already denotes a handle, so it cannot be combined with '*'" +
                                 location());
        }
        if (!type.isStruct()) {
            throw ParseException("'own' applies to struct types, not to '" +
                                 b::ast::typeToString(type) + "'" + location());
        }
        type.ownership = b::ast::Ownership::Owned;
        type.pointerLevel = 1;
    }

    if (borrowing) {
        if (type.isVoid()) {
            throw ParseException("There is nothing to borrow in 'void'" + location());
        }
        type.ownership = borrowingMutably ? b::ast::Ownership::MutBorrow
                                          : b::ast::Ownership::SharedBorrow;
        type.pointerLevel++;
    }

    if (match(b::lexer::TokenType::QUESTION)) {
        if (type.ownership == b::ast::Ownership::Value) {
            throw ParseException("'?' applies to 'own' and borrow types, not to '" +
                                 b::ast::typeToString(type) + "'" + location());
        }
        type.optional = true;
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
    size_t nodeStart = current;
    match(b::lexer::TokenType::KW_PUB);
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

    return makeNode<b::ast::StructDecl>(nodeStart, structName, std::move(fields));
}

void Parser::parseEnumDecl() {
    consume(b::lexer::TokenType::KW_ENUM, "Expected 'enum'");
    consume(b::lexer::TokenType::IDENTIFIER, "Expected enum name");
    consume(b::lexer::TokenType::LBRACE, "Expected '{' after enum name");

    while (!check(b::lexer::TokenType::RBRACE) && !isAtEnd()) {
        advance();
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
    size_t nodeStart = current;
    bool isPublic = match(b::lexer::TokenType::KW_PUB);
    b::ast::Type returnType = parseType();
    std::string functionName = consume(b::lexer::TokenType::IDENTIFIER, "Expected function name").lexeme;

    consume(b::lexer::TokenType::LPAREN, "Expected '(' after function name");
    auto parameters = parseParameterList();
    consume(b::lexer::TokenType::RPAREN, "Expected ')' after parameters");

    if (check(b::lexer::TokenType::SEMICOLON)) {
        throw ParseException("'" + functionName +
                             "' has no body. B needs no forward declarations: a function may be "
                             "called before the line that defines it, in any file" + location());
    }
    consume(b::lexer::TokenType::LBRACE, "Expected '{' before the body of '" + functionName + "'" +
                                             location());
    auto body = std::unique_ptr<b::ast::Block>(
        dynamic_cast<b::ast::Block*>(parseBlock().release())
    );
    consume(b::lexer::TokenType::RBRACE, "Expected '}' after function body");

    auto decl = makeNode<b::ast::FunctionDecl>(nodeStart,
        returnType, functionName, std::move(parameters), std::move(body)
    );
    decl->isPublic = isPublic;
    return decl;
}

std::unique_ptr<b::ast::FunctionDecl> Parser::parseDropDecl() {
    size_t nodeStart = current;
    consume(b::lexer::TokenType::KW_DROP, "Expected 'drop'");
    std::string typeName = consume(b::lexer::TokenType::IDENTIFIER,
                                   "Expected a struct name after 'drop'" + location()).lexeme;

    consume(b::lexer::TokenType::LPAREN, "Expected '(' after 'drop " + typeName + "'" + location());
    auto parameters = parseParameterList();
    consume(b::lexer::TokenType::RPAREN, "Expected ')' after the drop parameter" + location());

    if (parameters.size() != 1) {
        throw ParseException("'drop " + typeName + "' takes exactly one parameter, the value being dropped" +
                             location());
    }
    if (parameters[0].type.structName != typeName || !parameters[0].type.isMutBorrow() ||
        parameters[0].type.optional) {
        throw ParseException("'drop " + typeName + "' must take a '&mut " + typeName + "'" +
                             location());
    }

    consume(b::lexer::TokenType::LBRACE, "Expected '{' before the drop body" + location());
    auto body = std::unique_ptr<b::ast::Block>(
        dynamic_cast<b::ast::Block*>(parseBlock().release()));
    consume(b::lexer::TokenType::RBRACE, "Expected '}' after the drop body" + location());

    b::ast::Type returnType;
    returnType.base = b::ast::PrimitiveType::VOID;

    auto decl = makeNode<b::ast::FunctionDecl>(nodeStart, returnType, "drop$" + typeName,
                                               std::move(parameters), std::move(body));
    decl->dropsType = typeName;
    return decl;
}

bool Parser::looksLikeDeclarationStart() const {
    if (check(b::lexer::TokenType::KW_OWN)) {
        return true;
    }
    if (check(b::lexer::TokenType::AMPERSAND)) {
        size_t probe = current + 1;
        if (probe < tokens.size() && tokens[probe].type == b::lexer::TokenType::KW_MUT) {
            ++probe;
        }
        if (probe + 1 < tokens.size() &&
            tokens[probe + 1].type == b::lexer::TokenType::IDENTIFIER) {
            return true;
        }
    }
    if (check(b::lexer::TokenType::KW_INT) ||
        check(b::lexer::TokenType::KW_FLOAT) ||
        check(b::lexer::TokenType::KW_DOUBLE) ||
        check(b::lexer::TokenType::KW_BOOL) ||
        check(b::lexer::TokenType::KW_CHAR) ||
        check(b::lexer::TokenType::KW_STRING) ||
        check(b::lexer::TokenType::KW_VOID)) {
        return true;
    }

    if (!check(b::lexer::TokenType::IDENTIFIER)) {
        return false;
    }

    if (enumConstants.count(peek().lexeme) > 0) {
        return false;
    }

    if (genericStructs.count(peek().lexeme) > 0 &&
        current + 1 < tokens.size() &&
        tokens[current + 1].type == b::lexer::TokenType::LESS) {
        return true;
    }

    size_t lookAhead = current + 1;
    while (lookAhead < tokens.size() &&
           (tokens[lookAhead].type == b::lexer::TokenType::STAR ||
            tokens[lookAhead].type == b::lexer::TokenType::AMPERSAND)) {
        lookAhead++;
    }
    return lookAhead < tokens.size() &&
           tokens[lookAhead].type == b::lexer::TokenType::IDENTIFIER;
}

std::unique_ptr<b::ast::Statement> Parser::parseStatement() {
    size_t nodeStart = current;
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
        check(b::lexer::TokenType::KW_VOID) ||
        check(b::lexer::TokenType::KW_OWN)) {
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
        return makeNode<b::ast::VariableDecl>(nodeStart, type, name, std::move(init), true);
    }
    if ((check(b::lexer::TokenType::IDENTIFIER) || check(b::lexer::TokenType::AMPERSAND)) &&
        looksLikeDeclarationStart()) {
        return parseVariableDecl();
    }
    if (check(b::lexer::TokenType::KW_IF) && current + 1 < tokens.size() &&
        tokens[current + 1].type == b::lexer::TokenType::KW_SOME) {
        return parseIfSomeStmt();
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
        return makeNode<b::ast::BreakStmt>(nodeStart);
    }
    if (match(b::lexer::TokenType::KW_CONTINUE)) {
        consume(b::lexer::TokenType::SEMICOLON, "Expected ';' after 'continue'");
        return makeNode<b::ast::ContinueStmt>(nodeStart);
    }

    return parseExpressionStmt();
}

std::unique_ptr<b::ast::Statement> Parser::parseBlock() {
    size_t nodeStart = current;
    std::vector<std::unique_ptr<b::ast::Statement>> statements;

    while (!check(b::lexer::TokenType::RBRACE) && !isAtEnd()) {
        statements.push_back(parseStatement());
    }

    return makeNode<b::ast::Block>(nodeStart, std::move(statements));
}

std::unique_ptr<b::ast::Statement> Parser::parseVariableDecl() {
    size_t nodeStart = current;
    b::ast::Type type = parseType();
    std::string name = consume(b::lexer::TokenType::IDENTIFIER, "Expected variable name" + location()).lexeme;

    int arraySize = 0;
    if (match(b::lexer::TokenType::LBRACKET)) {
        b::lexer::Token sizeToken = consume(b::lexer::TokenType::INTEGER,
                                            "Expected a constant size in array declaration" + location());
        arraySize = std::stoi(sizeToken.value);
        if (arraySize <= 0) {
            throw ParseException("Array size must be greater than zero" + location());
        }
        consume(b::lexer::TokenType::RBRACKET, "Expected ']' after array size" + location());
        type.pointerLevel++;
    }

    std::unique_ptr<b::ast::Expression> initializer = nullptr;
    if (match(b::lexer::TokenType::EQUAL)) {
        if (arraySize > 0) {
            throw ParseException("Fixed-size arrays cannot have an initializer" + location());
        }
        initializer = parseExpression();
    }

    consume(b::lexer::TokenType::SEMICOLON, "Expected ';' after variable declaration" + location());
    return makeNode<b::ast::VariableDecl>(nodeStart, type, name, std::move(initializer), false, arraySize);
}

std::unique_ptr<b::ast::Statement> Parser::parseIfStmt() {
    size_t nodeStart = current;
    consume(b::lexer::TokenType::LPAREN, "Expected '(' after 'if'");
    auto condition = parseExpression();
    consume(b::lexer::TokenType::RPAREN, "Expected ')' after if condition");

    auto thenBranch = parseStatement();

    std::unique_ptr<b::ast::Statement> elseBranch = nullptr;
    if (match(b::lexer::TokenType::KW_ELSE)) {
        elseBranch = parseStatement();
    }

    return makeNode<b::ast::IfStmt>(nodeStart,
        std::move(condition), std::move(thenBranch), std::move(elseBranch)
    );
}

std::unique_ptr<b::ast::Statement> Parser::parseIfSomeStmt() {
    size_t nodeStart = current;
    consume(b::lexer::TokenType::KW_IF, "Expected 'if'");
    consume(b::lexer::TokenType::KW_SOME, "Expected 'some' after 'if'");
    bool mutably = match(b::lexer::TokenType::KW_MUT);

    consume(b::lexer::TokenType::LPAREN, "Expected '(' after 'if some'" + location());
    std::string binding = consume(b::lexer::TokenType::IDENTIFIER,
                                  "Expected a name to bind the unwrapped value to" + location()).lexeme;
    consume(b::lexer::TokenType::EQUAL, "Expected '=' after '" + binding + "'" + location());
    auto source = parseExpression();
    consume(b::lexer::TokenType::RPAREN, "Expected ')' after the unwrapped value" + location());

    auto thenBranch = parseStatement();
    std::unique_ptr<b::ast::Statement> elseBranch = nullptr;
    if (match(b::lexer::TokenType::KW_ELSE)) {
        elseBranch = parseStatement();
    }

    return makeNode<b::ast::IfSomeStmt>(nodeStart, binding, mutably, std::move(source),
                                        std::move(thenBranch), std::move(elseBranch));
}

std::unique_ptr<b::ast::Statement> Parser::parseForStmt() {
    size_t nodeStart = current;
    consume(b::lexer::TokenType::LPAREN, "Expected '(' after 'for'");

    std::unique_ptr<b::ast::Statement> init = nullptr;
    if (match(b::lexer::TokenType::SEMICOLON)) {
        init = nullptr;
    } else if (looksLikeDeclarationStart()) {
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

    return makeNode<b::ast::ForStmt>(nodeStart,
        std::move(init), std::move(condition), std::move(increment), std::move(body)
    );
}

std::unique_ptr<b::ast::Statement> Parser::parseWhileStmt() {
    size_t nodeStart = current;
    consume(b::lexer::TokenType::LPAREN, "Expected '(' after 'while'");
    auto condition = parseExpression();
    consume(b::lexer::TokenType::RPAREN, "Expected ')' after while condition");

    auto body = parseStatement();

    return makeNode<b::ast::WhileStmt>(nodeStart, std::move(condition), std::move(body));
}

std::unique_ptr<b::ast::Statement> Parser::parseSwitchStmt() {
    size_t nodeStart = current;
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
    return makeNode<b::ast::SwitchStmt>(nodeStart, std::move(condition), std::move(cases));
}

std::unique_ptr<b::ast::Statement> Parser::parseReturnStmt() {
    size_t nodeStart = current;
    std::unique_ptr<b::ast::Expression> value = nullptr;

    if (!check(b::lexer::TokenType::SEMICOLON)) {
        value = parseExpression();
    }

    consume(b::lexer::TokenType::SEMICOLON, "Expected ';' after return statement");
    return makeNode<b::ast::ReturnStmt>(nodeStart, std::move(value));
}

std::unique_ptr<b::ast::Statement> Parser::parseExpressionStmt() {
    size_t nodeStart = current;
    auto expr = parseExpression();
    consume(b::lexer::TokenType::SEMICOLON, "Expected ';' after expression");
    return makeNode<b::ast::ExpressionStmt>(nodeStart, std::move(expr));
}

std::unique_ptr<b::ast::Expression> Parser::parseExpression() {
    size_t nodeStart = current;
    return parseAssignment();
}

std::unique_ptr<b::ast::Expression> Parser::parseAssignment() {
    size_t nodeStart = current;
    auto expr = parseLogicalOr();

    if (match(b::lexer::TokenType::EQUAL)) {
        auto right = parseAssignment();
        return makeNode<b::ast::BinaryOp>(nodeStart,
            b::ast::BinaryOp::Operator::ASSIGN,
            std::move(expr),
            std::move(right)
        );
    }

    return expr;
}

std::unique_ptr<b::ast::Expression> Parser::parseLogicalOr() {
    size_t nodeStart = current;
    auto expr = parseLogicalAnd();

    while (match(b::lexer::TokenType::PIPE_PIPE)) {
        auto right = parseLogicalAnd();
        expr = makeNode<b::ast::BinaryOp>(nodeStart,
            b::ast::BinaryOp::Operator::LOGICAL_OR,
            std::move(expr),
            std::move(right)
        );
    }

    return expr;
}

std::unique_ptr<b::ast::Expression> Parser::parseLogicalAnd() {
    size_t nodeStart = current;
    auto expr = parseBitwiseOr();

    while (match(b::lexer::TokenType::AND_AND)) {
        auto right = parseBitwiseOr();
        expr = makeNode<b::ast::BinaryOp>(nodeStart,
            b::ast::BinaryOp::Operator::LOGICAL_AND,
            std::move(expr),
            std::move(right)
        );
    }

    return expr;
}

std::unique_ptr<b::ast::Expression> Parser::parseBitwiseOr() {
    size_t nodeStart = current;
    auto expr = parseBitwiseXor();

    while (match(b::lexer::TokenType::PIPE)) {
        auto right = parseBitwiseXor();
        expr = makeNode<b::ast::BinaryOp>(nodeStart,
            b::ast::BinaryOp::Operator::BITWISE_OR,
            std::move(expr),
            std::move(right)
        );
    }

    return expr;
}

std::unique_ptr<b::ast::Expression> Parser::parseBitwiseXor() {
    size_t nodeStart = current;
    auto expr = parseBitwiseAnd();

    while (match(b::lexer::TokenType::CARET)) {
        auto right = parseBitwiseAnd();
        expr = makeNode<b::ast::BinaryOp>(nodeStart,
            b::ast::BinaryOp::Operator::BITWISE_XOR,
            std::move(expr),
            std::move(right)
        );
    }

    return expr;
}

std::unique_ptr<b::ast::Expression> Parser::parseBitwiseAnd() {
    size_t nodeStart = current;
    auto expr = parseEquality();

    while (match(b::lexer::TokenType::AMPERSAND)) {
        auto right = parseEquality();
        expr = makeNode<b::ast::BinaryOp>(nodeStart,
            b::ast::BinaryOp::Operator::BITWISE_AND,
            std::move(expr),
            std::move(right)
        );
    }

    return expr;
}

std::unique_ptr<b::ast::Expression> Parser::parseEquality() {
    size_t nodeStart = current;
    auto expr = parseComparison();

    while (true) {
        if (match(b::lexer::TokenType::EQUAL_EQUAL)) {
            auto right = parseComparison();
            expr = makeNode<b::ast::BinaryOp>(nodeStart,
                b::ast::BinaryOp::Operator::EQUAL,
                std::move(expr),
                std::move(right)
            );
        } else if (match(b::lexer::TokenType::NOT_EQUAL)) {
            auto right = parseComparison();
            expr = makeNode<b::ast::BinaryOp>(nodeStart,
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

std::unique_ptr<b::ast::Expression> Parser::parseShift() {
    size_t nodeStart = current;
    auto expr = parseAdditive();

    while (true) {
        if (match(b::lexer::TokenType::LESS_LESS)) {
            auto right = parseAdditive();
            expr = makeNode<b::ast::BinaryOp>(nodeStart,
                b::ast::BinaryOp::Operator::SHIFT_LEFT,
                std::move(expr),
                std::move(right)
            );
        } else if (match(b::lexer::TokenType::GREATER_GREATER)) {
            auto right = parseAdditive();
            expr = makeNode<b::ast::BinaryOp>(nodeStart,
                b::ast::BinaryOp::Operator::SHIFT_RIGHT,
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
    size_t nodeStart = current;
    auto expr = parseShift();

    while (true) {
        if (match(b::lexer::TokenType::LESS)) {
            auto right = parseShift();
            expr = makeNode<b::ast::BinaryOp>(nodeStart,
                b::ast::BinaryOp::Operator::LESS,
                std::move(expr),
                std::move(right)
            );
        } else if (match(b::lexer::TokenType::LESS_EQUAL)) {
            auto right = parseShift();
            expr = makeNode<b::ast::BinaryOp>(nodeStart,
                b::ast::BinaryOp::Operator::LESS_EQUAL,
                std::move(expr),
                std::move(right)
            );
        } else if (match(b::lexer::TokenType::GREATER)) {
            auto right = parseShift();
            expr = makeNode<b::ast::BinaryOp>(nodeStart,
                b::ast::BinaryOp::Operator::GREATER,
                std::move(expr),
                std::move(right)
            );
        } else if (match(b::lexer::TokenType::GREATER_EQUAL)) {
            auto right = parseShift();
            expr = makeNode<b::ast::BinaryOp>(nodeStart,
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
    size_t nodeStart = current;
    auto expr = parseMultiplicative();

    while (true) {
        if (match(b::lexer::TokenType::PLUS)) {
            auto right = parseMultiplicative();
            expr = makeNode<b::ast::BinaryOp>(nodeStart,
                b::ast::BinaryOp::Operator::PLUS,
                std::move(expr),
                std::move(right)
            );
        } else if (match(b::lexer::TokenType::MINUS)) {
            auto right = parseMultiplicative();
            expr = makeNode<b::ast::BinaryOp>(nodeStart,
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
    size_t nodeStart = current;
    auto expr = parseUnary();

    while (true) {
        if (match(b::lexer::TokenType::STAR)) {
            auto right = parseUnary();
            expr = makeNode<b::ast::BinaryOp>(nodeStart,
                b::ast::BinaryOp::Operator::MULTIPLY,
                std::move(expr),
                std::move(right)
            );
        } else if (match(b::lexer::TokenType::SLASH)) {
            auto right = parseUnary();
            expr = makeNode<b::ast::BinaryOp>(nodeStart,
                b::ast::BinaryOp::Operator::DIVIDE,
                std::move(expr),
                std::move(right)
            );
        } else if (match(b::lexer::TokenType::PERCENT)) {
            auto right = parseUnary();
            expr = makeNode<b::ast::BinaryOp>(nodeStart,
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
    size_t nodeStart = current;
    if (check(b::lexer::TokenType::KW_NEW) && current + 1 < tokens.size() &&
        tokens[current + 1].type == b::lexer::TokenType::LBRACKET) {
        advance();
        advance();
        b::ast::Type element = parseType();
        consume(b::lexer::TokenType::RBRACKET, "Expected ']' after the element type" + location());
        if (element.slice || element.isBorrow()) {
            throw ParseException("A slice holds values or owned handles, not '" +
                                 b::ast::typeToString(element) + "'" + location());
        }
        consume(b::lexer::TokenType::LPAREN, "Expected '(' with the element count" + location());
        size_t countStart = current;
        auto count = parseExpression();
        size_t countEnd = current;
        consume(b::lexer::TokenType::RPAREN, "Expected ')' after the element count" + location());

        int staticLength = -1;
        if (countEnd == countStart + 1 &&
            tokens[countStart].type == b::lexer::TokenType::INTEGER) {
            try {
                long long value = std::stoll(tokens[countStart].value);
                if (value >= 0 && value <= 2147483647LL) {
                    staticLength = static_cast<int>(value);
                }
            } catch (const std::exception&) {
            }
        } else if (countEnd == countStart + 1 &&
                   tokens[countStart].type == b::lexer::TokenType::IDENTIFIER) {
            auto known = constantLengths.find(tokens[countStart].lexeme);
            if (known != constantLengths.end()) {
                staticLength = known->second;
            }
        }

        bool holdsOwned = element.isOwned();
        element.slice = true;
        element.ownedElements = holdsOwned;
        element.fixedLength = staticLength;
        element.pointerLevel = 1;
        element.ownership = b::ast::Ownership::Owned;
        element.optional = false;
        return makeNode<b::ast::NewSliceExpr>(nodeStart, element, std::move(count));
    }

    if (check(b::lexer::TokenType::KW_NEW)) {
        advance();
        b::ast::Type target = parseType();
        if (!target.isStruct() || target.pointerLevel != 0) {
            throw ParseException("'new' needs a struct type, got '" + b::ast::typeToString(target) +
                                 "'" + location());
        }
        target.ownership = b::ast::Ownership::Owned;
        target.pointerLevel = 1;

        std::vector<std::pair<std::string, std::unique_ptr<b::ast::Expression>>> fields;
        if (match(b::lexer::TokenType::LBRACE)) {
            while (!check(b::lexer::TokenType::RBRACE) && !isAtEnd()) {
                std::string field = consume(b::lexer::TokenType::IDENTIFIER,
                                            "Expected a field name in 'new'" + location()).lexeme;
                consume(b::lexer::TokenType::COLON, "Expected ':' after field name" + location());
                fields.emplace_back(field, parseExpression());
                if (!match(b::lexer::TokenType::COMMA)) {
                    break;
                }
            }
            consume(b::lexer::TokenType::RBRACE, "Expected '}' after field initializers" + location());
        }
        return makeNode<b::ast::NewExpr>(nodeStart, target, std::move(fields));
    }

    if (match(b::lexer::TokenType::KW_SIZEOF)) {
        consume(b::lexer::TokenType::LPAREN, "Expected '(' after 'sizeof'" + location());
        b::ast::Type type = parseType();
        consume(b::lexer::TokenType::RPAREN, "Expected ')' after sizeof type" + location());
        return makeNode<b::ast::SizeofExpr>(nodeStart, type);
    }

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
            check(b::lexer::TokenType::KW_VOID) ||
            (check(b::lexer::TokenType::IDENTIFIER) &&
             (enumTypeNames.count(peek().lexeme) > 0 ||
              funcPointerTypedefNames.count(peek().lexeme) > 0));

        if (looksLikeType) {
            b::ast::Type castType = parseType();
            if (check(b::lexer::TokenType::RPAREN)) {
                advance();
                auto operand = parseUnary();
                return makeNode<b::ast::CastExpr>(nodeStart, castType, std::move(operand));
            }
        }

        current = checkpoint;
    }

    if (match(b::lexer::TokenType::MINUS)) {
        auto operand = parseUnary();
        return makeNode<b::ast::UnaryOp>(nodeStart,
            b::ast::UnaryOp::Operator::NEGATE,
            std::move(operand)
        );
    }

    if (match(b::lexer::TokenType::BANG)) {
        auto operand = parseUnary();
        return makeNode<b::ast::UnaryOp>(nodeStart,
            b::ast::UnaryOp::Operator::NOT,
            std::move(operand)
        );
    }

    if (match(b::lexer::TokenType::TILDE)) {
        auto operand = parseUnary();
        return makeNode<b::ast::UnaryOp>(nodeStart,
            b::ast::UnaryOp::Operator::BITWISE_NOT,
            std::move(operand)
        );
    }

    if (match(b::lexer::TokenType::STAR)) {
        auto operand = parseUnary();
        return makeNode<b::ast::UnaryOp>(nodeStart,
            b::ast::UnaryOp::Operator::DEREF,
            std::move(operand)
        );
    }

    if (match(b::lexer::TokenType::AMPERSAND)) {
        bool mutably = match(b::lexer::TokenType::KW_MUT);
        auto operand = parseUnary();
        auto borrow = makeNode<b::ast::UnaryOp>(nodeStart,
            b::ast::UnaryOp::Operator::ADDRESS_OF,
            std::move(operand)
        );
        borrow->mutableBorrow = mutably;
        return borrow;
    }

    return parsePostfix();
}

std::unique_ptr<b::ast::Expression> Parser::parsePostfix() {
    size_t nodeStart = current;
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
                expr = makeNode<b::ast::FunctionCall>(nodeStart, funcName, std::move(arguments));
            } else {

                expr = makeNode<b::ast::FunctionCall>(nodeStart, std::move(expr), std::move(arguments));
            }
        } else if (match(b::lexer::TokenType::DOT) || match(b::lexer::TokenType::ARROW)) {
            std::string memberName = consume(b::lexer::TokenType::IDENTIFIER, "Expected member name after '.'/'->'").lexeme;
            expr = makeNode<b::ast::MemberAccess>(nodeStart, std::move(expr), memberName);
        } else if (match(b::lexer::TokenType::LBRACKET)) {
            auto indexExpr = parseExpression();
            consume(b::lexer::TokenType::RBRACKET, "Expected ']' after array index");
            expr = makeNode<b::ast::ArrayAccess>(nodeStart, std::move(expr), std::move(indexExpr));
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<b::ast::Expression> Parser::parsePrimary() {
    size_t nodeStart = current;
    if (match(b::lexer::TokenType::KW_NONE)) {
        return makeNode<b::ast::Literal>(nodeStart, b::ast::Literal::Kind::NONE, "none");
    }

    if (match(b::lexer::TokenType::KW_TRUE)) {
        return makeNode<b::ast::Literal>(nodeStart, b::ast::Literal::Kind::BOOLEAN, "true");
    }

    if (match(b::lexer::TokenType::KW_FALSE)) {
        return makeNode<b::ast::Literal>(nodeStart, b::ast::Literal::Kind::BOOLEAN, "false");
    }

    if (match(b::lexer::TokenType::INTEGER)) {
        return makeNode<b::ast::Literal>(nodeStart,
            b::ast::Literal::Kind::INTEGER, previous().value
        );
    }

    if (match(b::lexer::TokenType::CHAR_LITERAL)) {
        return makeNode<b::ast::Literal>(nodeStart,
            b::ast::Literal::Kind::INTEGER, previous().value
        );
    }

    if (match(b::lexer::TokenType::FLOAT)) {
        return makeNode<b::ast::Literal>(nodeStart,
            b::ast::Literal::Kind::FLOAT, previous().value
        );
    }

    if (match(b::lexer::TokenType::STRING)) {
        if (check(b::lexer::TokenType::STRING)) {
            throw ParseException("Two string literals side by side are not joined in B; "
                                 "write one literal, or build it with text::concat" + location());
        }
        return makeNode<b::ast::Literal>(nodeStart,
            b::ast::Literal::Kind::STRING, previous().value
        );
    }

    if (check(b::lexer::TokenType::IDENTIFIER) &&
        genericFunctions.count(peek().lexeme) > 0 &&
        current + 1 < tokens.size() &&
        tokens[current + 1].type == b::lexer::TokenType::LESS) {
        std::string templateName = advance().lexeme;
        std::vector<b::ast::Type> typeArgs = parseGenericArgList();
        return makeNode<b::ast::Identifier>(nodeStart,
            requestInstantiation(templateName, false, typeArgs)
        );
    }

    if (check(b::lexer::TokenType::IDENTIFIER)) {
        auto enumIt = enumConstants.find(peek().lexeme);
        if (enumIt != enumConstants.end()) {
            std::string owner = enumConstantOwner[peek().lexeme];
            advance();
            return makeNode<b::ast::Literal>(nodeStart,
                b::ast::Literal::Kind::INTEGER, std::to_string(enumIt->second), owner
            );
        }
    }

    if (check(b::lexer::TokenType::IDENTIFIER) && genericFunctions.count(peek().lexeme) > 0) {
        throw ParseException("Generic function '" + peek().lexeme +
                             "' needs type arguments, for example " + peek().lexeme + "<int>(...)" + location());
    }

    if (match(b::lexer::TokenType::IDENTIFIER)) {
        return makeNode<b::ast::Identifier>(nodeStart, previous().lexeme);
    }

    if (match(b::lexer::TokenType::LPAREN)) {
        auto expr = parseExpression();
        consume(b::lexer::TokenType::RPAREN, "Expected ')' after expression");
        return expr;
    }

    throw ParseException("Unexpected token: " + peek().lexeme);
}

}

namespace b::sema {

class Analyzer : public b::ast::ASTVisitor {
public:
    explicit Analyzer(b::diag::Reporter& reporter) : reporter(reporter) {}

    void run(b::ast::Program* program);

private:
    struct Local {
        std::string name;
        b::ast::Type type;
        b::ast::SourceLocation declaredAt;
        bool isParameter = false;
        bool read = false;
        bool written = false;
        bool moved = false;
        b::ast::SourceLocation movedAt;
        int sharedBorrows = 0;
        bool mutBorrowed = false;
        b::ast::SourceLocation borrowedAt;
        std::string borrowsFrom;
        bool borrowsMutably = false;
    };

    struct MoveState {
        bool moved = false;
        b::ast::SourceLocation movedAt;
    };
    using MoveSnapshot = std::vector<std::vector<MoveState>>;

    struct GlobalInfo {
        b::ast::Type type;
        b::ast::SourceLocation declaredAt;
        bool isPublic = false;
        bool read = false;
    };

    struct FunctionInfo {
        b::ast::SourceLocation declaredAt;
        bool isPublic = false;
        bool called = false;
        size_t parameterCount = 0;
        std::vector<b::ast::Type> parameterTypes;
        b::ast::Type returnType;
    };

    b::diag::Reporter& reporter;

    std::vector<std::vector<Local>> scopes;
    std::unordered_map<std::string, GlobalInfo> globals;
    std::unordered_map<std::string, FunctionInfo> functions;
    std::unordered_set<std::string> structNames;
    std::unordered_set<std::string> structsWithDrop;
    std::unordered_set<std::string> builtins;
    std::string currentFunction;

    static bool isIgnored(const std::string& name) { return !name.empty() && name[0] == '_'; }

    void pushScope() { scopes.emplace_back(); }
    void popScope();

    Local* findLocal(const std::string& name);
    void declareLocal(const std::string& name, const b::ast::Type& type,
                      const b::ast::SourceLocation& where, bool isParameter);
    void useName(const std::string& name, const b::ast::SourceLocation& where);
    void assignName(const std::string& name, const b::ast::SourceLocation& where);
    std::vector<std::string> visibleNames() const;

    void error(const b::ast::SourceLocation& where, const std::string& text,
               const std::string& help = "");
    void walk(b::ast::Expression* expr);
    void walk(b::ast::Statement* stmt);

    void consume(b::ast::Expression* expr, const std::string& what);
    static std::string borrowRoot(b::ast::Expression* expr);
    std::string optionalPayloadStruct(b::ast::Expression* expr);
    void rejectOptionalAccess(b::ast::Expression* target, const std::string& what);
    bool typeOf(b::ast::Expression* expr, b::ast::Type& out);
    std::unordered_map<std::string, std::unordered_map<std::string, b::ast::Type>> structFieldTypes;
    size_t scopeIndexOf(const std::string& name) const;
    void openBorrow(b::ast::UnaryOp* node);
    void registerNamedBorrow(const std::string& borrower, b::ast::Expression* initializer);
    void releaseBorrow(const Local& borrower);
    MoveSnapshot captureMoves() const;
    void restoreMoves(const MoveSnapshot& snapshot);
    void mergeMoves(const MoveSnapshot& branch);
    void reportMovesInLoop(const MoveSnapshot& before);
    b::ast::Type currentReturnType;
    bool pathTerminated = false;

    void visit(b::ast::Literal* node) override;
    void visit(b::ast::SizeofExpr* node) override;
    void visit(b::ast::NewExpr* node) override;
    void visit(b::ast::NewSliceExpr* node) override;
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
    void visit(b::ast::IfSomeStmt* node) override;
    void visit(b::ast::ForStmt* node) override;
    void visit(b::ast::WhileStmt* node) override;
    void visit(b::ast::BreakStmt* node) override;
    void visit(b::ast::ContinueStmt* node) override;
    void visit(b::ast::SwitchStmt* node) override;
    void visit(b::ast::StructDecl* node) override;
    void visit(b::ast::FunctionDecl* node) override;
    void visit(b::ast::Program* node) override;
};

void Analyzer::error(const b::ast::SourceLocation& where, const std::string& text,
                     const std::string& help) {
    reporter.error(where.file, where.line, where.column, text, help);
}

void Analyzer::walk(b::ast::Expression* expr) {
    if (expr) {
        expr->accept(this);
    }
}

void Analyzer::walk(b::ast::Statement* stmt) {
    if (stmt) {
        stmt->accept(this);
    }
}

std::vector<std::string> Analyzer::visibleNames() const {
    std::vector<std::string> names;
    for (const auto& scope : scopes) {
        for (const auto& local : scope) {
            names.push_back(local.name);
        }
    }
    for (const auto& entry : globals) {
        names.push_back(entry.first);
    }
    for (const auto& entry : functions) {
        names.push_back(entry.first);
    }
    return names;
}

Analyzer::Local* Analyzer::findLocal(const std::string& name) {
    for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
        for (auto local = scope->rbegin(); local != scope->rend(); ++local) {
            if (local->name == name) {
                return &*local;
            }
        }
    }
    return nullptr;
}

void Analyzer::declareLocal(const std::string& name, const b::ast::Type& type,
                            const b::ast::SourceLocation& where, bool isParameter) {
    if (scopes.empty()) {
        return;
    }
    for (const auto& existing : scopes.back()) {
        if (existing.name == name) {
            error(where, "'" + name + "' is already declared in this scope",
                  "the earlier declaration is at line " + std::to_string(existing.declaredAt.line));
            return;
        }
    }
    scopes.back().push_back({name, type, where, isParameter, false, false});
}

void Analyzer::popScope() {
    if (scopes.empty()) {
        return;
    }
    for (const auto& local : scopes.back()) {
        releaseBorrow(local);
    }
    for (const auto& local : scopes.back()) {
        if (local.read || isIgnored(local.name)) {
            continue;
        }
        if (structsWithDrop.count(local.type.structName)) {
            continue;
        }
        std::string kind = local.isParameter ? "parameter" : "variable";
        std::string help = "remove it, or rename it to '_" + local.name +
                           "' if it is deliberately unused";
        if (local.written && !local.isParameter) {
            error(local.declaredAt, "'" + local.name + "' is assigned but its value is never read",
                  help);
        } else {
            error(local.declaredAt, "unused " + kind + " '" + local.name + "'", help);
        }
    }
    scopes.pop_back();
}

void Analyzer::useName(const std::string& name, const b::ast::SourceLocation& where) {
    if (Local* local = findLocal(name)) {
        local->read = true;
        if (local->moved) {
            error(where, "'" + name + "' is used after it was moved",
                  "it was moved at line " + std::to_string(local->movedAt.line));
        } else if (local->mutBorrowed) {
            error(where, "cannot use '" + name + "' while it is mutably borrowed",
                  "the borrow starts at line " + std::to_string(local->borrowedAt.line));
        }
        return;
    }
    auto globalIt = globals.find(name);
    if (globalIt != globals.end()) {
        globalIt->second.read = true;
        return;
    }
    auto functionIt = functions.find(name);
    if (functionIt != functions.end()) {
        functionIt->second.called = true;
        return;
    }
    if (builtins.count(name) || structNames.count(name)) {
        return;
    }

    std::string suggestion = b::diag::closestMatch(name, visibleNames());
    error(where, "cannot find '" + name + "' in this scope",
          suggestion.empty() ? "" : "did you mean '" + suggestion + "'?");
}

void Analyzer::assignName(const std::string& name, const b::ast::SourceLocation& where) {
    if (Local* local = findLocal(name)) {
        local->written = true;
        return;
    }
    auto globalIt = globals.find(name);
    if (globalIt != globals.end()) {
        return;
    }
    useName(name, where);
}


Analyzer::MoveSnapshot Analyzer::captureMoves() const {
    MoveSnapshot snapshot;
    snapshot.reserve(scopes.size());
    for (const auto& scope : scopes) {
        std::vector<MoveState> states;
        states.reserve(scope.size());
        for (const auto& local : scope) {
            states.push_back({local.moved, local.movedAt});
        }
        snapshot.push_back(std::move(states));
    }
    return snapshot;
}

void Analyzer::restoreMoves(const MoveSnapshot& snapshot) {
    for (size_t i = 0; i < scopes.size() && i < snapshot.size(); ++i) {
        for (size_t j = 0; j < scopes[i].size() && j < snapshot[i].size(); ++j) {
            scopes[i][j].moved = snapshot[i][j].moved;
            scopes[i][j].movedAt = snapshot[i][j].movedAt;
        }
    }
}

void Analyzer::mergeMoves(const MoveSnapshot& branch) {
    for (size_t i = 0; i < scopes.size() && i < branch.size(); ++i) {
        for (size_t j = 0; j < scopes[i].size() && j < branch[i].size(); ++j) {
            if (branch[i][j].moved && !scopes[i][j].moved) {
                scopes[i][j].moved = true;
                scopes[i][j].movedAt = branch[i][j].movedAt;
            }
        }
    }
}

void Analyzer::reportMovesInLoop(const MoveSnapshot& before) {
    for (size_t i = 0; i < scopes.size() && i < before.size(); ++i) {
        for (size_t j = 0; j < scopes[i].size() && j < before[i].size(); ++j) {
            if (scopes[i][j].moved && !before[i][j].moved) {
                error(scopes[i][j].movedAt,
                      "'" + scopes[i][j].name + "' is moved inside a loop, so the next iteration "
                      "would move it again",
                      "move it outside the loop, or create a new value each iteration");
            }
        }
    }
}

void Analyzer::consume(b::ast::Expression* expr, const std::string& what) {
    auto* ident = dynamic_cast<b::ast::Identifier*>(expr);
    if (!ident) {
        bool isPlace = dynamic_cast<b::ast::MemberAccess*>(expr) != nullptr ||
                       dynamic_cast<b::ast::ArrayAccess*>(expr) != nullptr;
        b::ast::Type placeType;
        if (isPlace && typeOf(expr, placeType) && placeType.isOwned()) {
            std::string root = borrowRoot(expr);
            error(expr->loc, "cannot move ownership out of '" + root + "'",
                  "borrow it with '&' instead, or copy the value");
            return;
        }
        walk(expr);
        return;
    }
    Local* local = findLocal(ident->name);
    if (!local || !local->type.isOwned()) {
        walk(expr);
        return;
    }
    if (local->moved) {
        error(ident->loc, "'" + ident->name + "' has already been moved",
              "it was moved at line " + std::to_string(local->movedAt.line));
        return;
    }
    if (local->mutBorrowed || local->sharedBorrows > 0) {
        error(ident->loc, "cannot move '" + ident->name + "' while it is borrowed",
              "the borrow starts at line " + std::to_string(local->borrowedAt.line));
        return;
    }
    local->read = true;
    local->moved = true;
    local->movedAt = ident->loc;
    ident->isMoveSource = true;
    (void)what;
}


bool Analyzer::typeOf(b::ast::Expression* expr, b::ast::Type& out) {
    if (auto* ident = dynamic_cast<b::ast::Identifier*>(expr)) {
        if (Local* local = findLocal(ident->name)) {
            out = local->type;
            return true;
        }
        auto globalIt = globals.find(ident->name);
        if (globalIt != globals.end()) {
            out = globalIt->second.type;
            return true;
        }
        return false;
    }
    if (auto* member = dynamic_cast<b::ast::MemberAccess*>(expr)) {
        b::ast::Type objectType;
        if (!typeOf(member->object.get(), objectType) || objectType.structName.empty()) {
            return false;
        }
        auto structIt = structFieldTypes.find(objectType.structName);
        if (structIt == structFieldTypes.end()) {
            return false;
        }
        auto fieldIt = structIt->second.find(member->member);
        if (fieldIt == structIt->second.end()) {
            return false;
        }
        out = fieldIt->second;
        return true;
    }
    if (auto* element = dynamic_cast<b::ast::ArrayAccess*>(expr)) {
        b::ast::Type arrayType;
        if (!typeOf(element->array.get(), arrayType) || !arrayType.slice) {
            return false;
        }
        bool holdsOwned = arrayType.ownedElements;
        out = arrayType;
        out.slice = false;
        out.ownedElements = false;
        out.fixedLength = -1;
        out.optional = false;
        out.ownership = holdsOwned ? b::ast::Ownership::Owned : b::ast::Ownership::Value;
        out.pointerLevel = holdsOwned ? 1 : 0;
        return true;
    }
    if (auto* unary = dynamic_cast<b::ast::UnaryOp*>(expr)) {
        if (unary->op == b::ast::UnaryOp::Operator::ADDRESS_OF) {
            return typeOf(unary->operand.get(), out);
        }
        return false;
    }
    if (auto* call = dynamic_cast<b::ast::FunctionCall*>(expr)) {
        auto it = functions.find(call->functionName);
        if (it == functions.end()) {
            return false;
        }
        out = it->second.returnType;
        return true;
    }
    return false;
}

std::string Analyzer::optionalPayloadStruct(b::ast::Expression* expr) {
    b::ast::Type type;
    if (!typeOf(expr, type) || !type.optional) {
        return "";
    }
    return type.structName;
}

std::string Analyzer::borrowRoot(b::ast::Expression* expr) {
    if (auto* ident = dynamic_cast<b::ast::Identifier*>(expr)) {
        return ident->name;
    }
    if (auto* member = dynamic_cast<b::ast::MemberAccess*>(expr)) {
        return borrowRoot(member->object.get());
    }
    if (auto* element = dynamic_cast<b::ast::ArrayAccess*>(expr)) {
        return borrowRoot(element->array.get());
    }
    if (auto* unary = dynamic_cast<b::ast::UnaryOp*>(expr)) {
        if (unary->op == b::ast::UnaryOp::Operator::DEREF ||
            unary->op == b::ast::UnaryOp::Operator::ADDRESS_OF) {
            return borrowRoot(unary->operand.get());
        }
    }
    return "";
}

size_t Analyzer::scopeIndexOf(const std::string& name) const {
    for (size_t i = scopes.size(); i > 0; --i) {
        for (const auto& local : scopes[i - 1]) {
            if (local.name == name) {
                return i - 1;
            }
        }
    }
    return scopes.size();
}

void Analyzer::openBorrow(b::ast::UnaryOp* node) {
    std::string root = borrowRoot(node->operand.get());
    if (root.empty()) {
        return;
    }
    Local* target = findLocal(root);
    if (!target) {
        return;
    }
    if (target->moved) {
        error(node->loc, "cannot borrow '" + root + "' because it has been moved",
              "it was moved at line " + std::to_string(target->movedAt.line));
        return;
    }
    if (node->mutableBorrow) {
        if (target->mutBorrowed) {
            error(node->loc, "'" + root + "' is already mutably borrowed",
                  "the earlier borrow starts at line " + std::to_string(target->borrowedAt.line));
        } else if (target->sharedBorrows > 0) {
            error(node->loc, "cannot borrow '" + root + "' mutably while it is borrowed",
                  "the shared borrow starts at line " + std::to_string(target->borrowedAt.line));
        }
    } else if (target->mutBorrowed) {
        error(node->loc, "cannot borrow '" + root + "' while it is mutably borrowed",
              "the mutable borrow starts at line " + std::to_string(target->borrowedAt.line));
    }
}

void Analyzer::registerNamedBorrow(const std::string& borrower, b::ast::Expression* initializer) {
    auto* unary = dynamic_cast<b::ast::UnaryOp*>(initializer);
    if (!unary || unary->op != b::ast::UnaryOp::Operator::ADDRESS_OF) {
        return;
    }
    std::string root = borrowRoot(unary->operand.get());
    if (root.empty()) {
        return;
    }
    Local* target = findLocal(root);
    Local* holder = findLocal(borrower);
    if (!target || !holder) {
        return;
    }

    if (scopeIndexOf(root) > scopeIndexOf(borrower)) {
        error(unary->loc, "'" + borrower + "' would outlive '" + root + "'",
              "'" + root + "' is released at the end of its block, leaving the borrow dangling");
        return;
    }

    holder->borrowsFrom = root;
    holder->borrowsMutably = unary->mutableBorrow;
    if (unary->mutableBorrow) {
        target->mutBorrowed = true;
    } else {
        target->sharedBorrows++;
    }
    target->borrowedAt = unary->loc;
}

void Analyzer::releaseBorrow(const Local& borrower) {
    if (borrower.borrowsFrom.empty()) {
        return;
    }
    if (Local* target = findLocal(borrower.borrowsFrom)) {
        if (borrower.borrowsMutably) {
            target->mutBorrowed = false;
        } else if (target->sharedBorrows > 0) {
            target->sharedBorrows--;
        }
    }
}

void Analyzer::visit(b::ast::Literal* node) { (void)node; }

void Analyzer::visit(b::ast::SizeofExpr* node) { (void)node; }

void Analyzer::visit(b::ast::NewSliceExpr* node) { walk(node->count.get()); }

void Analyzer::visit(b::ast::NewExpr* node) {
    auto structIt = structFieldTypes.find(node->type.structName);
    for (const auto& field : node->fields) {
        bool takesOwnership = false;
        if (structIt != structFieldTypes.end()) {
            auto fieldIt = structIt->second.find(field.first);
            takesOwnership = fieldIt != structIt->second.end() && fieldIt->second.isOwned();
        }
        if (takesOwnership) {
            consume(field.second.get(), "field initializer");
        } else {
            walk(field.second.get());
        }
    }
}

void Analyzer::visit(b::ast::Identifier* node) { useName(node->name, node->loc); }

void Analyzer::visit(b::ast::BinaryOp* node) {
    if (node->op == b::ast::BinaryOp::Operator::ASSIGN) {

        if (!dynamic_cast<b::ast::Identifier*>(node->left.get())) {
            std::string root = borrowRoot(node->left.get());
            if (Local* holder = findLocal(root)) {
                if (holder->type.isSharedBorrow()) {
                    error(node->loc, "cannot assign through '" + root +
                                         "' because it is a shared borrow",
                          "take it as '&mut " +
                              b::ast::typeToString([&] {
                                  b::ast::Type bare = holder->type;
                                  bare.ownership = b::ast::Ownership::Value;
                                  bare.pointerLevel = bare.pointerLevel > 0 ? bare.pointerLevel - 1 : 0;
                                  return bare;
                              }()) +
                              "' if it needs to change");
                }
            }
        }

        bool targetOwns = false;
        b::ast::Type placeType;
        if (typeOf(node->left.get(), placeType) && placeType.isOwned()) {
            targetOwns = true;
        }
        if (auto* target = dynamic_cast<b::ast::Identifier*>(node->left.get())) {
            if (Local* local = findLocal(target->name)) {
                targetOwns = targetOwns || local->type.isOwned();
                if (targetOwns && local->moved) {
                    local->moved = false;
                }
            }
            assignName(target->name, target->loc);
        } else {
            walk(node->left.get());
        }
        if (targetOwns) {
            consume(node->right.get(), "assignment");
        } else {
            walk(node->right.get());
        }
        if (auto* target = dynamic_cast<b::ast::Identifier*>(node->left.get())) {
            if (Local* holder = findLocal(target->name)) {
                if (holder->type.isBorrow()) {
                    releaseBorrow(*holder);
                    holder->borrowsFrom.clear();
                    registerNamedBorrow(target->name, node->right.get());
                }
            }
        }
        return;
    }
    walk(node->left.get());
    walk(node->right.get());
}

void Analyzer::visit(b::ast::UnaryOp* node) {
    if (node->op == b::ast::UnaryOp::Operator::ADDRESS_OF) {
        openBorrow(node);
        if (auto* ident = dynamic_cast<b::ast::Identifier*>(node->operand.get())) {
            if (Local* local = findLocal(ident->name)) {
                local->read = true;
                return;
            }
        }
    }
    walk(node->operand.get());
}

void Analyzer::visit(b::ast::CastExpr* node) { walk(node->expr.get()); }

void Analyzer::visit(b::ast::FunctionCall* node) {
    if (node->isIndirect()) {
        walk(node->callee.get());
    } else {
        auto it = functions.find(node->functionName);
        if (it != functions.end()) {
            it->second.called = true;
            if (node->arguments.size() != it->second.parameterCount) {
                error(node->loc, "'" + node->functionName + "' expects " +
                                     std::to_string(it->second.parameterCount) +
                                     " argument(s) but got " +
                                     std::to_string(node->arguments.size()));
            }
        } else if (!builtins.count(node->functionName) && !findLocal(node->functionName) &&
                   !globals.count(node->functionName)) {
            std::vector<std::string> names;
            for (const auto& entry : functions) {
                names.push_back(entry.first);
            }
            for (const auto& name : builtins) {
                names.push_back(name);
            }
            std::string suggestion = b::diag::closestMatch(node->functionName, names);
            error(node->loc, "cannot find function '" + node->functionName + "'",
                  suggestion.empty() ? "" : "did you mean '" + suggestion + "'?");
        } else if (Local* local = findLocal(node->functionName)) {
            local->read = true;
        }
    }
    auto signature = functions.find(node->functionName);
    for (size_t i = 0; i < node->arguments.size(); ++i) {
        bool consumesArgument = signature != functions.end() &&
                                i < signature->second.parameterTypes.size() &&
                                signature->second.parameterTypes[i].isOwned();
        if (consumesArgument) {
            consume(node->arguments[i].get(), "argument");
        } else {
            walk(node->arguments[i].get());
        }
    }
}

void Analyzer::visit(b::ast::MemberAccess* node) {
    rejectOptionalAccess(node->object.get(), "reach '" + node->member + "' through");
    walk(node->object.get());
}

void Analyzer::visit(b::ast::ArrayAccess* node) {
    rejectOptionalAccess(node->array.get(), "index");
    walk(node->array.get());
    walk(node->index.get());
}

void Analyzer::rejectOptionalAccess(b::ast::Expression* target, const std::string& what) {
    b::ast::Type targetType;
    if (!typeOf(target, targetType) || !targetType.optional) {
        return;
    }
    std::string root = borrowRoot(target);
    error(target->loc,
          "cannot " + what + " '" + b::ast::typeToString(targetType) +
              "' without unwrapping it first",
          root.empty() ? "wrap the access in 'if some (value = ...) { ... }'"
                       : "write 'if some (value = " + root + ") { ... }' and use 'value' inside");
}

void Analyzer::visit(b::ast::VariableDecl* node) {
    if (node->type.isOwned()) {
        consume(node->initializer.get(), "initializer");
    } else {
        walk(node->initializer.get());
    }
    declareLocal(node->name, node->type, node->loc, false);
    if (node->type.isBorrow() && node->initializer) {
        registerNamedBorrow(node->name, node->initializer.get());
    }
}

void Analyzer::visit(b::ast::ReturnStmt* node) {
    if (currentReturnType.isOwned()) {
        consume(node->value.get(), "return value");
    } else {
        walk(node->value.get());
    }

    if (currentReturnType.isBorrow() && node->value) {
        std::string root = borrowRoot(node->value.get());
        Local* source = root.empty() ? nullptr : findLocal(root);
        if (source && !source->type.isBorrow()) {
            error(node->loc, "cannot return a borrow of '" + root + "'",
                  "'" + root + "' is released when this function returns, so the borrow would dangle");
        }
    }

    pathTerminated = true;
}

void Analyzer::visit(b::ast::ExpressionStmt* node) { walk(node->expression.get()); }

void Analyzer::visit(b::ast::Block* node) {
    pushScope();
    for (const auto& statement : node->statements) {
        if (pathTerminated) {
            break;
        }
        walk(statement.get());
    }
    popScope();
}

void Analyzer::visit(b::ast::IfSomeStmt* node) {
    walk(node->source.get());

    b::ast::Type bound;
    std::string root = borrowRoot(node->source.get());
    if (Local* holder = findLocal(root)) {
        holder->read = true;
    }
    bound.base = b::ast::PrimitiveType::INT;
    bound.pointerLevel = 1;
    bound.ownership = node->mutableBinding ? b::ast::Ownership::MutBorrow
                                           : b::ast::Ownership::SharedBorrow;
    bound.structName = optionalPayloadStruct(node->source.get());
    if (bound.structName.empty()) {
        error(node->loc, "'if some' needs an optional value to unwrap",
              "declare the value as 'own T?' or '&T?'");
    }

    MoveSnapshot beforeBranches = captureMoves();

    pushScope();
    declareLocal(node->binding, bound, node->loc, false);
    pathTerminated = false;
    walk(node->thenBranch.get());
    bool thenLeft = pathTerminated;
    popScope();
    MoveSnapshot afterThen = captureMoves();

    restoreMoves(beforeBranches);
    pathTerminated = false;
    walk(node->elseBranch.get());
    bool elseLeft = pathTerminated;

    if (!thenLeft) {
        mergeMoves(afterThen);
    }

    pathTerminated = thenLeft && elseLeft && node->elseBranch != nullptr;
}

void Analyzer::visit(b::ast::IfStmt* node) {
    walk(node->condition.get());

    MoveSnapshot beforeBranches = captureMoves();

    pathTerminated = false;
    walk(node->thenBranch.get());
    bool thenLeft = pathTerminated;
    MoveSnapshot afterThen = captureMoves();

    restoreMoves(beforeBranches);
    pathTerminated = false;
    walk(node->elseBranch.get());
    bool elseLeft = pathTerminated;

    if (!thenLeft) {
        mergeMoves(afterThen);
    }

    pathTerminated = thenLeft && elseLeft && node->elseBranch != nullptr;
}

void Analyzer::visit(b::ast::ForStmt* node) {
    pushScope();
    walk(node->init.get());
    walk(node->condition.get());
    walk(node->increment.get());

    MoveSnapshot beforeBody = captureMoves();
    walk(node->body.get());
    reportMovesInLoop(beforeBody);
    pathTerminated = false;

    popScope();
}

void Analyzer::visit(b::ast::WhileStmt* node) {
    walk(node->condition.get());

    MoveSnapshot beforeBody = captureMoves();
    walk(node->body.get());
    reportMovesInLoop(beforeBody);
    pathTerminated = false;
}

void Analyzer::visit(b::ast::BreakStmt* node) {
    (void)node;
    pathTerminated = true;
}

void Analyzer::visit(b::ast::ContinueStmt* node) {
    (void)node;
    pathTerminated = true;
}

void Analyzer::visit(b::ast::SwitchStmt* node) {
    walk(node->condition.get());

    MoveSnapshot beforeCases = captureMoves();
    MoveSnapshot combined = beforeCases;
    for (const auto& caseItem : node->cases) {
        restoreMoves(beforeCases);
        walk(caseItem.value.get());
        pushScope();
        pathTerminated = false;
        for (const auto& statement : caseItem.statements) {
            if (pathTerminated) {
                break;
            }
            walk(statement.get());
        }
        popScope();
        pathTerminated = false;
        MoveSnapshot afterCase = captureMoves();
        restoreMoves(combined);
        mergeMoves(afterCase);
        combined = captureMoves();
    }
    restoreMoves(combined);
}

void Analyzer::visit(b::ast::StructDecl* node) { (void)node; }

void Analyzer::visit(b::ast::FunctionDecl* node) {
    currentFunction = node->name;
    currentReturnType = node->returnType;
    pathTerminated = false;
    pushScope();
    for (const auto& parameter : node->parameters) {
        declareLocal(parameter.name, parameter.type, node->loc, true);
    }
    if (node->body) {

        for (const auto& statement : node->body->statements) {
            walk(statement.get());
        }
    }
    popScope();
    currentFunction.clear();
}

void Analyzer::visit(b::ast::Program* node) { (void)node; }

void Analyzer::run(b::ast::Program* program) {
    builtins = {"print",  "println", "printf", "scanf",  "sizeof", "len",
                "strlen", "strcmp",  "strcpy", "atoi",   "itoa",   "sprintf",
                "b_read", "b_write", "b_open", "b_close"};

    for (const auto& strct : program->structs) {
        structNames.insert(strct->name);
        for (const auto& field : strct->fields) {
            structFieldTypes[strct->name][field.name] = field.type;
        }
    }
    for (const auto& enumDecl : program->enums) {
        structNames.insert(enumDecl.name);
        for (const auto& constant : enumDecl.constants) {
            builtins.insert(constant.name);
        }
    }
    for (const auto& typedefDecl : program->funcPointerTypedefs) {
        structNames.insert(typedefDecl.name);
    }
    for (const auto& global : program->globalVariables) {
        globals[global.name] = {global.type, global.loc, global.isPublic, false};
    }
    for (const auto& function : program->functions) {
        FunctionInfo info;
        info.declaredAt = function->loc;
        info.isPublic = function->isPublic || !function->dropsType.empty();
        info.parameterCount = function->parameters.size();
        info.returnType = function->returnType;
        for (const auto& parameter : function->parameters) {
            info.parameterTypes.push_back(parameter.type);
        }
        functions[function->name] = std::move(info);
    }

    std::unordered_map<std::string, b::ast::SourceLocation> dropOwners;
    for (const auto& function : program->functions) {
        if (!function->dropsType.empty()) {
            structsWithDrop.insert(function->dropsType);
        }
    }
    for (const auto& function : program->functions) {
        if (function->dropsType.empty()) {
            continue;
        }
        if (!structNames.count(function->dropsType)) {
            error(function->loc, "'drop " + function->dropsType + "' names '" + function->dropsType +
                                     "', which is not a struct",
                  b::diag::closestMatch(function->dropsType,
                                        std::vector<std::string>(structNames.begin(), structNames.end()))
                      .empty()
                      ? ""
                      : "did you mean '" +
                            b::diag::closestMatch(
                                function->dropsType,
                                std::vector<std::string>(structNames.begin(), structNames.end())) +
                            "'?");
            continue;
        }
        auto existing = dropOwners.find(function->dropsType);
        if (existing != dropOwners.end()) {
            error(function->loc, "struct '" + function->dropsType + "' already has a drop function",
                  "the first one is at line " + std::to_string(existing->second.line));
            continue;
        }
        dropOwners[function->dropsType] = function->loc;
    }

    for (const auto& global : program->globalVariables) {
        if (global.initializer) {
            walk(global.initializer.get());
        }
    }
    for (const auto& function : program->functions) {
        function->accept(this);
    }

    for (const auto& function : program->functions) {
        const FunctionInfo& info = functions[function->name];
        if (info.called || info.isPublic || function->name == "main" || isIgnored(function->name)) {
            continue;
        }
        error(info.declaredAt, "unused function '" + function->name + "'",
              "call it, or mark it 'pub' if it is part of this module's public surface");
    }

    for (const auto& global : program->globalVariables) {
        const GlobalInfo& info = globals[global.name];
        if (info.read || info.isPublic || isIgnored(global.name)) {
            continue;
        }
        error(info.declaredAt, "unused global '" + global.name + "'",
              "remove it, or mark it 'pub' if it is part of this module's public surface");
    }
}

}

namespace b::codegen {

int32_t parseIntLiteral(const std::string& text) {
    long long value = 0;
    try {
        size_t consumed = 0;
        value = std::stoll(text, &consumed);
        if (consumed != text.size()) {
            throw std::invalid_argument("trailing characters");
        }
    } catch (const std::exception&) {
        throw b::CompilerException("'" + text + "' is not a valid integer literal");
    }
    if (value < INT32_MIN || value > INT32_MAX) {
        throw b::CompilerException("Integer literal " + text +
                                   " does not fit in 'int' (-2147483648 .. 2147483647)");
    }
    return static_cast<int32_t>(value);
}

llvm::ConstantInt* castConstantInt(llvm::ConstantInt* value, llvm::Type* target, bool isSigned) {
    unsigned width = target->getIntegerBitWidth();
    llvm::APInt bits = value->getValue();
    return llvm::ConstantInt::get(target->getContext(),
                                  isSigned ? bits.sextOrTrunc(width) : bits.zextOrTrunc(width));
}

double parseFloatLiteral(const std::string& text) {
    try {
        size_t consumed = 0;
        double value = std::stod(text, &consumed);
        if (consumed != text.size()) {
            throw std::invalid_argument("trailing characters");
        }
        return value;
    } catch (const std::exception&) {
        throw b::CompilerException("'" + text + "' is not a valid floating-point literal");
    }
}

class CodeGenerator : public b::ast::ASTVisitor {
public:
    CodeGenerator();
    ~CodeGenerator();

    bool generate(b::ast::Program* program, const std::string& outputPath);
    bool emitObject(const std::string& outputPath);
    void optimize();
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
    std::unordered_map<std::string, std::unordered_map<std::string, b::ast::Type>> structFieldTypes;
    std::unordered_map<std::string, llvm::FunctionType*> funcPointerTypedefs;
    std::unordered_map<std::string, b::ast::Type> funcPointerReturnTypes;
    std::unordered_set<std::string> constVariables;
    std::unordered_set<std::string> constGlobals;
    std::unordered_map<std::string, llvm::Constant*> constGlobalValues;

    struct OwnedLocal {
        std::string name;
        std::string structName;
        b::ast::Type type;
        bool isSlice = false;
        llvm::Value* slot = nullptr;
        llvm::Value* flag = nullptr;
    };
    std::vector<std::vector<OwnedLocal>> ownedScopes;
    std::unordered_map<std::string, llvm::Value*> dropFlags;
    std::unordered_map<std::string, llvm::Function*> userDropFunctions;
    std::unordered_map<std::string, llvm::Function*> dropGlueFunctions;
    std::vector<size_t> loopOwnedDepth;
    std::unordered_map<std::string, llvm::GlobalVariable*> globalVariables;
    std::unordered_map<std::string, b::ast::Type> globalVariableTypes;
    std::unordered_map<std::string, b::ast::Type> functionReturnTypes;
    std::unordered_map<std::string, std::vector<b::ast::Type>> functionParamTypes;
    std::unordered_map<std::string, std::vector<b::ast::EnumConstant>> enumMembers;
    std::unordered_set<std::string> definedFunctions;
    b::ast::Type currentReturnType;
    llvm::Function* currentFunction;
    llvm::Value* lastValue;
    std::vector<llvm::BasicBlock*> breakTargets;
    std::vector<llvm::BasicBlock*> continueTargets;

    std::string enumTypeOf(b::ast::Expression* expr);
    void checkEnumCompatible(const b::ast::Type& target, b::ast::Expression* value,
                             const std::string& context);
    void checkEnumOperands(b::ast::BinaryOp* node);

    llvm::Type* arcTypeToLLVM(const b::ast::Type& type);
    b::ast::Type derefType(const b::ast::Type& type);
    llvm::FunctionType* createFunctionType(const b::ast::FunctionDecl* decl);
    void declareBuiltins();
    void linkAllocatorRuntime();
    llvm::AllocaInst* createEntryAlloca(llvm::Type* type, const std::string& name);
    llvm::Constant* createStringConstant(const std::string& text);
    llvm::Constant* evalConstantExpr(b::ast::Expression* expr, const std::string& where);
    llvm::Value* emitPointerArithmetic(b::ast::BinaryOp* node, llvm::Value* lhs, llvm::Value* rhs,
                                       const b::ast::Type& leftType, const b::ast::Type& rightType);
    void emitShortCircuit(b::ast::BinaryOp* node);
    void checkStructCycles(b::ast::Program* program);
    llvm::Function* dropGlueFor(const std::string& structName);
    static b::ast::Type elementTypeOf(const b::ast::Type& sliceType);
    static bool assignableFrom(const b::ast::Type& target, const b::ast::Type& source);
    void emitDrop(const OwnedLocal& local);
    void emitSliceElementDrops(llvm::Value* slice, const b::ast::Type& sliceType);
    void emitBoundsCheck(llvm::Value* index, llvm::Value* length);
    void emitScopeDrops(size_t fromDepth);
    void registerOwnedLocal(const std::string& name, const b::ast::Type& type, llvm::Value* slot,
                            bool initialized);
    void pushScope();
    void popScope();
    void setVariable(const std::string& name, llvm::Value* value);
    llvm::Value* getVariable(const std::string& name);
    bool inferType(b::ast::Expression* expr, b::ast::Type& outType);
    llvm::Value* addressOf(b::ast::Expression* expr, b::ast::Type* outType);
    llvm::Value* coerceValue(llvm::Value* value, llvm::Type* targetType);
    bool blockIsTerminated();
    llvm::Value* toBoolCondition(llvm::Value* value);

    void visit(b::ast::Literal* node) override;
    void visit(b::ast::SizeofExpr* node) override;
    void visit(b::ast::NewExpr* node) override;
    void visit(b::ast::NewSliceExpr* node) override;
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
    void visit(b::ast::IfSomeStmt* node) override;
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
    linkAllocatorRuntime();
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
    } catch (const b::CompilerException& ex) {
        llvm::errs() << "Error: " << ex.what() << "\n";
        return false;
    } catch (const std::exception& ex) {
        llvm::errs() << "Codegen Error: " << ex.what() << "\n";
        return false;
    }
}

void CodeGenerator::optimize() {
    llvm::LoopAnalysisManager loopAnalyses;
    llvm::FunctionAnalysisManager functionAnalyses;
    llvm::CGSCCAnalysisManager cgsccAnalyses;
    llvm::ModuleAnalysisManager moduleAnalyses;

    llvm::PassBuilder builder;
    builder.registerModuleAnalyses(moduleAnalyses);
    builder.registerCGSCCAnalyses(cgsccAnalyses);
    builder.registerFunctionAnalyses(functionAnalyses);
    builder.registerLoopAnalyses(loopAnalyses);
    builder.crossRegisterProxies(loopAnalyses, functionAnalyses, cgsccAnalyses, moduleAnalyses);

    llvm::ModulePassManager passes =
        builder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
    passes.run(*module, moduleAnalyses);
}

bool CodeGenerator::emitObject(const std::string& outputPath) {
    optimize();

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


static const char* kAllocatorRuntimeIR = R"IR(
@b_heap_cursor = internal global i64 0
@b_heap_limit = internal global i64 0
@b_free_lists = internal global [64 x i64] zeroinitializer
@b_msg_bounds = internal constant [32 x i8] c"B: index out of range, aborting\0A"
@b_msg_negative = internal constant [35 x i8] c"B: negative slice length, aborting\0A"

define internal i64 @b_os_alloc(i64 %len) {
  %r = call i64 asm sideeffect "syscall", "={ax},{ax},{di},{si},{dx},{r10},{r8},{r9},~{rcx},~{r11},~{memory},~{dirflag},~{fpsr},~{flags}"(i64 9, i64 0, i64 %len, i64 3, i64 34, i64 -1, i64 0)
  ret i64 %r
}

define internal void @b_os_release(i64 %addr, i64 %len) {
  %r = call i64 asm sideeffect "syscall", "={ax},{ax},{di},{si},~{rcx},~{r11},~{memory},~{dirflag},~{fpsr},~{flags}"(i64 11, i64 %addr, i64 %len)
  ret void
}

define internal i64 @b_class_of(i64 %total) {
entry:
  br label %probe
probe:
  %k = phi i64 [ 5, %entry ], [ %next, %grow ]
  %size = shl i64 1, %k
  %fits = icmp uge i64 %size, %total
  br i1 %fits, label %done, label %grow
grow:
  %next = add i64 %k, 1
  br label %probe
done:
  ret i64 %k
}

define ptr @b_alloc(i64 %size) {
entry:
  %need = add i64 %size, 16
  %k = call i64 @b_class_of(i64 %need)
  %blocksize = shl i64 1, %k
  %slot = getelementptr inbounds [64 x i64], ptr @b_free_lists, i64 0, i64 %k
  %head = load i64, ptr %slot
  %reusable = icmp ne i64 %head, 0
  br i1 %reusable, label %reuse, label %bump

reuse:
  %headptr = inttoptr i64 %head to ptr
  %linkslot = getelementptr inbounds i8, ptr %headptr, i64 8
  %link = load i64, ptr %linkslot
  store i64 %link, ptr %slot
  br label %handout

bump:
  %cursor = load i64, ptr @b_heap_cursor
  %limit = load i64, ptr @b_heap_limit
  %after = add i64 %cursor, %blocksize
  %roomy = icmp ule i64 %after, %limit
  br i1 %roomy, label %carve, label %refill

refill:
  %huge = icmp ugt i64 %blocksize, 1048576
  %chunk = select i1 %huge, i64 %blocksize, i64 1048576
  %base = call i64 @b_os_alloc(i64 %chunk)
  %failed = icmp ugt i64 %base, -4096
  br i1 %failed, label %exhausted, label %adopt

adopt:
  %newlimit = add i64 %base, %chunk
  store i64 %base, ptr @b_heap_cursor
  store i64 %newlimit, ptr @b_heap_limit
  br label %bump

exhausted:
  ret ptr null

carve:
  store i64 %after, ptr @b_heap_cursor
  br label %handout

handout:
  %block = phi i64 [ %head, %reuse ], [ %cursor, %carve ]
  %header = inttoptr i64 %block to ptr
  store i64 %k, ptr %header
  %payload = add i64 %block, 16
  %result = inttoptr i64 %payload to ptr
  ret ptr %result
}

define internal void @b_os_write(i64 %fd, ptr %buf, i64 %len) {
  %addr = ptrtoint ptr %buf to i64
  %r = call i64 asm sideeffect "syscall", "={ax},{ax},{di},{si},{dx},~{rcx},~{r11},~{memory},~{dirflag},~{fpsr},~{flags}"(i64 1, i64 %fd, i64 %addr, i64 %len)
  ret void
}

define i64 @b_write(i64 %fd, ptr %buf, i64 %len) {
  %addr = ptrtoint ptr %buf to i64
  %r = call i64 asm sideeffect "syscall", "={ax},{ax},{di},{si},{dx},~{rcx},~{r11},~{memory},~{dirflag},~{fpsr},~{flags}"(i64 1, i64 %fd, i64 %addr, i64 %len)
  ret i64 %r
}

define i64 @b_read(i64 %fd, ptr %buf, i64 %len) {
  %addr = ptrtoint ptr %buf to i64
  %r = call i64 asm sideeffect "syscall", "={ax},{ax},{di},{si},{dx},~{rcx},~{r11},~{memory},~{dirflag},~{fpsr},~{flags}"(i64 0, i64 %fd, i64 %addr, i64 %len)
  ret i64 %r
}

define i64 @b_open(ptr %path, i64 %flags, i64 %mode) {
  %addr = ptrtoint ptr %path to i64
  %r = call i64 asm sideeffect "syscall", "={ax},{ax},{di},{si},{dx},~{rcx},~{r11},~{memory},~{dirflag},~{fpsr},~{flags}"(i64 2, i64 %addr, i64 %flags, i64 %mode)
  ret i64 %r
}

define i64 @b_close(i64 %fd) {
  %r = call i64 asm sideeffect "syscall", "={ax},{ax},{di},~{rcx},~{r11},~{memory},~{dirflag},~{fpsr},~{flags}"(i64 3, i64 %fd)
  ret i64 %r
}

define void @b_panic(ptr %msg, i64 %len) noreturn {
  call void @b_os_write(i64 2, ptr %msg, i64 %len)
  %r = call i64 asm sideeffect "syscall", "={ax},{ax},{di},~{rcx},~{r11},~{memory},~{dirflag},~{fpsr},~{flags}"(i64 231, i64 134)
  unreachable
}

define i64 @b_len(ptr %p) {
entry:
  %empty = icmp eq ptr %p, null
  br i1 %empty, label %none, label %read
none:
  ret i64 0
read:
  %addr = ptrtoint ptr %p to i64
  %slot = sub i64 %addr, 8
  %slotptr = inttoptr i64 %slot to ptr
  %count = load i64, ptr %slotptr
  ret i64 %count
}

define ptr @b_alloc_array(i64 %elemSize, i64 %count) {
entry:
  %negative = icmp slt i64 %count, 0
  br i1 %negative, label %bad, label %ok
bad:
  call void @b_panic(ptr @b_msg_negative, i64 35)
  unreachable
ok:
  %bytes = mul i64 %elemSize, %count
  %block = call ptr @b_alloc(i64 %bytes)
  %missing = icmp eq ptr %block, null
  br i1 %missing, label %oom, label %record
oom:
  ret ptr null
record:
  %addr = ptrtoint ptr %block to i64
  %slot = sub i64 %addr, 8
  %slotptr = inttoptr i64 %slot to ptr
  store i64 %count, ptr %slotptr
  br label %wipe
wipe:
  %i = phi i64 [ 0, %record ], [ %next, %step ]
  %done = icmp uge i64 %i, %bytes
  br i1 %done, label %finished, label %step
step:
  %cell = getelementptr inbounds i8, ptr %block, i64 %i
  store i8 0, ptr %cell
  %next = add i64 %i, 1
  br label %wipe
finished:
  ret ptr %block
}

define void @b_bounds_check(i64 %index, i64 %len) {
entry:
  %below = icmp slt i64 %index, 0
  %above = icmp sge i64 %index, %len
  %bad = or i1 %below, %above
  br i1 %bad, label %fail, label %ok
fail:
  call void @b_panic(ptr @b_msg_bounds, i64 32)
  unreachable
ok:
  ret void
}

define void @b_free(ptr %p) {
entry:
  %empty = icmp eq ptr %p, null
  br i1 %empty, label %done, label %recycle
recycle:
  %addr = ptrtoint ptr %p to i64
  %block = sub i64 %addr, 16
  %header = inttoptr i64 %block to ptr
  %k = load i64, ptr %header
  %slot = getelementptr inbounds [64 x i64], ptr @b_free_lists, i64 0, i64 %k
  %head = load i64, ptr %slot
  %linkslot = getelementptr inbounds i8, ptr %header, i64 8
  store i64 %head, ptr %linkslot
  store i64 %block, ptr %slot
  br label %done
done:
  ret void
}
)IR";

void CodeGenerator::linkAllocatorRuntime() {
    llvm::SMDiagnostic parseError;
    auto runtime = llvm::parseAssemblyString(kAllocatorRuntimeIR, parseError, *context);
    if (!runtime) {
        std::string detail;
        llvm::raw_string_ostream stream(detail);
        parseError.print("b-runtime", stream);
        throw b::CompilerException("Internal error: allocator runtime failed to parse: " + detail);
    }
    runtime->setTargetTriple(module->getTargetTriple());
    runtime->setDataLayout(module->getDataLayout());
    if (llvm::Linker::linkModules(*module, std::move(runtime))) {
        throw b::CompilerException("Internal error: allocator runtime failed to link");
    }
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

llvm::AllocaInst* CodeGenerator::createEntryAlloca(llvm::Type* type, const std::string& name) {
    if (!currentFunction) {
        throw b::CompilerException("Cannot declare '" + name + "' outside of a function");
    }
    llvm::BasicBlock& entry = currentFunction->getEntryBlock();
    llvm::IRBuilder<> entryBuilder(&entry, entry.getFirstInsertionPt());
    return entryBuilder.CreateAlloca(type, nullptr, name);
}

llvm::Constant* CodeGenerator::createStringConstant(const std::string& text) {
    llvm::Constant* data = llvm::ConstantDataArray::getString(*context, text, true);
    auto* gv = new llvm::GlobalVariable(*module, data->getType(), true,
                                        llvm::GlobalValue::PrivateLinkage, data, ".str");
    gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    return gv;
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
    if (it != variables.end()) {
        return it->second;
    }

    auto globalIt = globalVariables.find(name);
    if (globalIt != globalVariables.end()) {
        return globalIt->second;
    }

    throw std::runtime_error("Undefined variable: " + name);
}

std::string CodeGenerator::enumTypeOf(b::ast::Expression* expr) {
    if (auto* literal = dynamic_cast<b::ast::Literal*>(expr)) {
        return literal->enumName;
    }

    if (auto* binary = dynamic_cast<b::ast::BinaryOp*>(expr)) {
        if (binary->op == b::ast::BinaryOp::Operator::ASSIGN) {
            return enumTypeOf(binary->left.get());
        }
        return "";
    }

    b::ast::Type resolved;
    if (inferType(expr, resolved) && resolved.isEnum()) {
        return resolved.enumName;
    }
    return "";
}

void CodeGenerator::checkEnumCompatible(const b::ast::Type& target, b::ast::Expression* value,
                                        const std::string& context) {
    std::string sourceEnum = enumTypeOf(value);

    if (target.isEnum()) {
        if (sourceEnum.empty()) {
            throw b::CompilerException("Cannot use a non-enum value for " + context +
                                       " of enum type '" + target.enumName +
                                       "'; cast explicitly with (" + target.enumName + ")");
        }
        if (sourceEnum != target.enumName) {
            throw b::CompilerException("Cannot use enum '" + sourceEnum + "' for " + context +
                                       " of enum type '" + target.enumName + "'");
        }
        return;
    }

    if (!sourceEnum.empty()) {
        throw b::CompilerException("Cannot use enum '" + sourceEnum + "' for " + context +
                                   " of type '" + b::ast::typeToString(target) +
                                   "'; cast explicitly with (int)");
    }
}

void CodeGenerator::checkEnumOperands(b::ast::BinaryOp* node) {
    std::string leftEnum = enumTypeOf(node->left.get());
    std::string rightEnum = enumTypeOf(node->right.get());

    if (leftEnum.empty() && rightEnum.empty()) {
        return;
    }

    bool isEquality = node->op == b::ast::BinaryOp::Operator::EQUAL ||
                      node->op == b::ast::BinaryOp::Operator::NOT_EQUAL;

    if (!isEquality) {
        throw b::CompilerException("Enum type '" + (leftEnum.empty() ? rightEnum : leftEnum) +
                                   "' supports only == and !=; cast to (int) for other operations");
    }

    if (leftEnum.empty() || rightEnum.empty()) {
        throw b::CompilerException("Cannot compare enum '" + (leftEnum.empty() ? rightEnum : leftEnum) +
                                   "' with a non-enum value");
    }

    if (leftEnum != rightEnum) {
        throw b::CompilerException("Cannot compare enum '" + leftEnum + "' with enum '" + rightEnum + "'");
    }
}

llvm::Constant* CodeGenerator::evalConstantExpr(b::ast::Expression* expr,
                                                const std::string& where) {
    if (auto* literal = dynamic_cast<b::ast::Literal*>(expr)) {
        switch (literal->kind) {
            case b::ast::Literal::Kind::INTEGER:
                return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context),
                                              parseIntLiteral(literal->value), true);
            case b::ast::Literal::Kind::FLOAT:
                return llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context),
                                             parseFloatLiteral(literal->value));
            case b::ast::Literal::Kind::BOOLEAN:
                return llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context),
                                              literal->value == "true");
            case b::ast::Literal::Kind::STRING:
                return createStringConstant(literal->value);
        }
    }

    if (auto* ident = dynamic_cast<b::ast::Identifier*>(expr)) {
        auto it = constGlobalValues.find(ident->name);
        if (it != constGlobalValues.end()) {
            return it->second;
        }
        if (globalVariables.count(ident->name)) {
            throw b::CompilerException("'" + ident->name + "' is not const, so it cannot be used in " +
                                       where + "; declare it as 'const'");
        }
        throw b::CompilerException("'" + ident->name + "' is not a constant known at compile time, " +
                                   "so it cannot be used in " + where);
    }

    if (auto* sizeExpr = dynamic_cast<b::ast::SizeofExpr*>(expr)) {
        llvm::Type* type = arcTypeToLLVM(sizeExpr->targetType);
        if (sizeExpr->targetType.isVoid() || !type->isSized()) {
            throw b::CompilerException("sizeof is not allowed on this type in " + where);
        }
        return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context),
                                      module->getDataLayout().getTypeAllocSize(type));
    }

    if (auto* unary = dynamic_cast<b::ast::UnaryOp*>(expr)) {
        llvm::Constant* operand = evalConstantExpr(unary->operand.get(), where);
        switch (unary->op) {
            case b::ast::UnaryOp::Operator::NEGATE:
                if (auto* asFloat = llvm::dyn_cast<llvm::ConstantFP>(operand)) {
                    return llvm::ConstantFP::get(asFloat->getType(),
                                                 -asFloat->getValueAPF().convertToDouble());
                }
                return llvm::ConstantExpr::getNeg(operand);
            case b::ast::UnaryOp::Operator::BITWISE_NOT:
                return llvm::ConstantExpr::getNot(operand);
            default:
                break;
        }
    }

    if (auto* binary = dynamic_cast<b::ast::BinaryOp*>(expr)) {
        llvm::Constant* lhs = evalConstantExpr(binary->left.get(), where);
        llvm::Constant* rhs = evalConstantExpr(binary->right.get(), where);
        if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
            auto* left = llvm::dyn_cast<llvm::ConstantInt>(lhs);
            auto* right = llvm::dyn_cast<llvm::ConstantInt>(rhs);
            if (left && right) {
                int64_t a = left->getSExtValue();
                int64_t b = right->getSExtValue();
                int64_t result = 0;
                switch (binary->op) {
                    case b::ast::BinaryOp::Operator::PLUS:     result = a + b; break;
                    case b::ast::BinaryOp::Operator::MINUS:    result = a - b; break;
                    case b::ast::BinaryOp::Operator::MULTIPLY: result = a * b; break;
                    case b::ast::BinaryOp::Operator::DIVIDE:
                    case b::ast::BinaryOp::Operator::MODULO:
                        if (b == 0) {
                            throw b::CompilerException("Division by zero in " + where);
                        }
                        result = binary->op == b::ast::BinaryOp::Operator::DIVIDE ? a / b : a % b;
                        break;
                    case b::ast::BinaryOp::Operator::SHIFT_LEFT:  result = a << b; break;
                    case b::ast::BinaryOp::Operator::SHIFT_RIGHT: result = a >> b; break;
                    case b::ast::BinaryOp::Operator::BITWISE_AND: result = a & b; break;
                    case b::ast::BinaryOp::Operator::BITWISE_OR:  result = a | b; break;
                    case b::ast::BinaryOp::Operator::BITWISE_XOR: result = a ^ b; break;
                    default:
                        throw b::CompilerException(
                            "This operator is not allowed in " + where +
                            "; global initializers must be compile-time constants");
                }
                return llvm::ConstantInt::get(lhs->getType(), result, true);
            }
        }
    }

    if (auto* cast = dynamic_cast<b::ast::CastExpr*>(expr)) {
        llvm::Constant* value = evalConstantExpr(cast->expr.get(), where);
        llvm::Type* target = arcTypeToLLVM(cast->targetType);
        if (value->getType() == target) {
            return value;
        }
        if (value->getType()->isIntegerTy() && target->isIntegerTy()) {
            return castConstantInt(llvm::cast<llvm::ConstantInt>(value), target,
                                   !value->getType()->isIntegerTy(1));
        }
    }

    throw b::CompilerException(
        where + " must be a compile-time constant; call the function from inside main() instead");
}

void CodeGenerator::visit(b::ast::SizeofExpr* node) {
    llvm::Type* type = arcTypeToLLVM(node->targetType);
    if (node->targetType.isVoid()) {
        throw b::CompilerException("sizeof(void) is not allowed");
    }
    if (!type->isSized()) {
        throw b::CompilerException("Cannot take sizeof an incomplete type: " +
                                   b::ast::typeToString(node->targetType));
    }

    uint64_t size = module->getDataLayout().getTypeAllocSize(type);
    lastValue = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context),
                                       static_cast<uint32_t>(size));
}


llvm::Function* CodeGenerator::dropGlueFor(const std::string& structName) {
    auto existing = dropGlueFunctions.find(structName);
    if (existing != dropGlueFunctions.end()) {
        return existing->second;
    }

    llvm::Type* ptrType = llvm::PointerType::get(*context, 0);
    llvm::FunctionType* glueType =
        llvm::FunctionType::get(llvm::Type::getVoidTy(*context), {ptrType}, false);
    llvm::Function* glue = llvm::Function::Create(glueType, llvm::Function::InternalLinkage,
                                                  "drop.glue." + structName, module.get());
    dropGlueFunctions[structName] = glue;

    llvm::BasicBlock* savedBlock = builder->GetInsertBlock();
    llvm::BasicBlock::iterator savedPoint;
    if (savedBlock) {
        savedPoint = builder->GetInsertPoint();
    }

    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context, "entry", glue);
    builder->SetInsertPoint(entry);
    llvm::Value* target = &*glue->arg_begin();

    auto userDrop = userDropFunctions.find(structName);
    if (userDrop != userDropFunctions.end()) {
        builder->CreateCall(userDrop->second, {target});
    }

    auto namesIt = structFields.find(structName);
    auto typesIt = structFieldTypes.find(structName);
    auto structIt = structTypes.find(structName);
    if (namesIt != structFields.end() && typesIt != structFieldTypes.end() &&
        structIt != structTypes.end()) {
        for (size_t i = 0; i < namesIt->second.size(); ++i) {
            const b::ast::Type& fieldType = typesIt->second[namesIt->second[i]];
            if (!fieldType.isOwned()) {
                continue;
            }
            llvm::Value* slot =
                builder->CreateStructGEP(structIt->second, target, static_cast<unsigned>(i));
            llvm::Value* owned = builder->CreateLoad(ptrType, slot);
            llvm::Value* present = builder->CreateICmpNE(
                owned, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrType)));

            llvm::BasicBlock* release = llvm::BasicBlock::Create(*context, "drop.field", glue);
            llvm::BasicBlock* skip = llvm::BasicBlock::Create(*context, "drop.skip", glue);
            builder->CreateCondBr(present, release, skip);

            builder->SetInsertPoint(release);
            if (fieldType.slice) {
                emitSliceElementDrops(owned, fieldType);
            } else {
                builder->CreateCall(dropGlueFor(fieldType.structName), {owned});
            }
            builder->CreateCall(module->getFunction("b_free"), {owned});
            builder->CreateBr(skip);

            builder->SetInsertPoint(skip);
        }
    }

    builder->CreateRetVoid();

    if (savedBlock) {
        builder->SetInsertPoint(savedBlock, savedPoint);
    } else {
        builder->ClearInsertionPoint();
    }
    return glue;
}

void CodeGenerator::emitBoundsCheck(llvm::Value* index, llvm::Value* length) {
    llvm::Function* host = builder->GetInsertBlock()->getParent();
    llvm::Value* inRange = builder->CreateICmpULT(index, length);

    llvm::BasicBlock* fail = llvm::BasicBlock::Create(*context, "index.fail", host);
    llvm::BasicBlock* proceed = llvm::BasicBlock::Create(*context, "index.ok", host);

    llvm::MDBuilder metadata(*context);
    llvm::BranchInst* branch = builder->CreateCondBr(inRange, proceed, fail);
    branch->setMetadata(llvm::LLVMContext::MD_prof, metadata.createBranchWeights(2000, 1));

    builder->SetInsertPoint(fail);
    llvm::Function* panic = module->getFunction("b_panic");
    llvm::GlobalVariable* message = module->getGlobalVariable("b_msg_bounds", true);
    builder->CreateCall(panic, {message, llvm::ConstantInt::get(
                                             llvm::Type::getInt64Ty(*context), 32)});
    builder->CreateUnreachable();

    builder->SetInsertPoint(proceed);
}

void CodeGenerator::emitSliceElementDrops(llvm::Value* slice, const b::ast::Type& sliceType) {
    b::ast::Type element = elementTypeOf(sliceType);
    bool ownedElements = element.isOwned();
    if (!ownedElements && element.structName.empty()) {
        return;
    }
    if (!ownedElements && !structTypes.count(element.structName)) {
        return;
    }
    llvm::Type* elementType = arcTypeToLLVM(element);

    llvm::Type* countType = llvm::Type::getInt64Ty(*context);
    llvm::Value* count = builder->CreateCall(module->getFunction("b_len"), {slice});

    llvm::Function* host = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock& hostEntry = host->getEntryBlock();
    llvm::IRBuilder<> entryBuilder(&hostEntry, hostEntry.getFirstInsertionPt());
    llvm::Value* cursor = entryBuilder.CreateAlloca(countType, nullptr, "slice.i");
    builder->CreateStore(llvm::ConstantInt::get(countType, 0), cursor);

    llvm::BasicBlock* test = llvm::BasicBlock::Create(*context, "slice.test", host);
    llvm::BasicBlock* body = llvm::BasicBlock::Create(*context, "slice.body", host);
    llvm::BasicBlock* done = llvm::BasicBlock::Create(*context, "slice.done", host);

    builder->CreateBr(test);
    builder->SetInsertPoint(test);
    llvm::Value* index = builder->CreateLoad(countType, cursor);
    builder->CreateCondBr(builder->CreateICmpULT(index, count), body, done);

    builder->SetInsertPoint(body);
    llvm::Value* slot = builder->CreateGEP(elementType, slice, index);
    if (ownedElements) {
        llvm::Type* ptrType = llvm::PointerType::get(*context, 0);
        llvm::Value* held = builder->CreateLoad(ptrType, slot);
        llvm::Value* present = builder->CreateICmpNE(
            held, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrType)));
        llvm::BasicBlock* release = llvm::BasicBlock::Create(*context, "slice.item", host);
        llvm::BasicBlock* skip = llvm::BasicBlock::Create(*context, "slice.skip", host);
        builder->CreateCondBr(present, release, skip);
        builder->SetInsertPoint(release);
        if (!element.structName.empty()) {
            builder->CreateCall(dropGlueFor(element.structName), {held});
        }
        builder->CreateCall(module->getFunction("b_free"), {held});
        builder->CreateBr(skip);
        builder->SetInsertPoint(skip);
    } else {
        builder->CreateCall(dropGlueFor(element.structName), {slot});
    }
    builder->CreateStore(builder->CreateAdd(index, llvm::ConstantInt::get(countType, 1)), cursor);
    builder->CreateBr(test);

    builder->SetInsertPoint(done);
}

void CodeGenerator::emitDrop(const OwnedLocal& local) {
    llvm::Type* ptrType = llvm::PointerType::get(*context, 0);
    llvm::Type* boolType = llvm::Type::getInt1Ty(*context);

    llvm::Value* live = builder->CreateLoad(boolType, local.flag);
    llvm::BasicBlock* release = llvm::BasicBlock::Create(*context, "drop.live", currentFunction);
    llvm::BasicBlock* after = llvm::BasicBlock::Create(*context, "drop.done", currentFunction);
    builder->CreateCondBr(live, release, after);

    builder->SetInsertPoint(release);
    llvm::Value* value = builder->CreateLoad(ptrType, local.slot);
    if (local.isSlice) {
        emitSliceElementDrops(value, local.type);
    } else {
        builder->CreateCall(dropGlueFor(local.structName), {value});
    }
    builder->CreateCall(module->getFunction("b_free"), {value});
    builder->CreateStore(llvm::ConstantInt::getFalse(*context), local.flag);
    builder->CreateBr(after);

    builder->SetInsertPoint(after);
}

void CodeGenerator::emitScopeDrops(size_t fromDepth) {
    for (size_t depth = ownedScopes.size(); depth > fromDepth; --depth) {
        const auto& scope = ownedScopes[depth - 1];
        for (auto local = scope.rbegin(); local != scope.rend(); ++local) {
            if (blockIsTerminated()) {
                return;
            }
            emitDrop(*local);
        }
    }
}

void CodeGenerator::registerOwnedLocal(const std::string& name, const b::ast::Type& type,
                                       llvm::Value* slot, bool initialized) {
    llvm::BasicBlock& entry = currentFunction->getEntryBlock();
    llvm::IRBuilder<> entryBuilder(&entry, entry.getFirstInsertionPt());
    llvm::Value* flag =
        entryBuilder.CreateAlloca(llvm::Type::getInt1Ty(*context), nullptr, name + ".live");
    entryBuilder.CreateStore(llvm::ConstantInt::getFalse(*context), flag);
    if (initialized) {
        builder->CreateStore(llvm::ConstantInt::getTrue(*context), flag);
    }
    dropFlags[name] = flag;
    if (!ownedScopes.empty()) {
        ownedScopes.back().push_back({name, type.structName, type, type.slice, slot, flag});
    }
}

bool CodeGenerator::assignableFrom(const b::ast::Type& target, const b::ast::Type& source) {
    if (target.slice != source.slice) {
        return false;
    }
    if (!target.slice) {
        return true;
    }
    if (b::ast::typeToString(elementTypeOf(target)) !=
        b::ast::typeToString(elementTypeOf(source))) {
        return false;
    }
    if (target.fixedLength >= 0 && target.fixedLength != source.fixedLength) {
        return false;
    }
    return true;
}

b::ast::Type CodeGenerator::elementTypeOf(const b::ast::Type& sliceType) {
    b::ast::Type element = sliceType;
    bool holdsOwned = sliceType.ownedElements;
    element.slice = false;
    element.ownedElements = false;
    element.fixedLength = -1;
    element.optional = false;
    element.ownership = holdsOwned ? b::ast::Ownership::Owned : b::ast::Ownership::Value;
    element.pointerLevel = holdsOwned ? 1 : 0;
    return element;
}

void CodeGenerator::visit(b::ast::NewSliceExpr* node) {
    b::ast::Type element = elementTypeOf(node->type);
    llvm::Type* elementType = arcTypeToLLVM(element);
    if (!elementType->isSized()) {
        throw b::CompilerException("Cannot allocate a slice of '" +
                                   b::ast::typeToString(element) + "'");
    }

    node->count->accept(this);
    llvm::Value* count = builder->CreateIntCast(lastValue, llvm::Type::getInt64Ty(*context), true);

    uint64_t stride = module->getDataLayout().getTypeAllocSize(elementType);
    llvm::Function* allocator = module->getFunction("b_alloc_array");
    if (!allocator) {
        throw b::CompilerException("Internal error: allocator runtime is missing");
    }
    lastValue = builder->CreateCall(
        allocator, {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), stride), count});
}

void CodeGenerator::visit(b::ast::NewExpr* node) {
    b::ast::Type valueType = node->type;
    valueType.pointerLevel = 0;
    valueType.ownership = b::ast::Ownership::Value;

    llvm::Type* structType = arcTypeToLLVM(valueType);
    if (!structType->isSized()) {
        throw b::CompilerException("Cannot allocate an incomplete type '" +
                                   b::ast::typeToString(valueType) + "'");
    }

    llvm::Function* allocator = module->getFunction("b_alloc");
    if (!allocator) {
        throw b::CompilerException("Internal error: allocator runtime is missing");
    }

    uint64_t size = module->getDataLayout().getTypeAllocSize(structType);
    llvm::Value* storage = builder->CreateCall(
        allocator, {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), size)});
    builder->CreateStore(llvm::Constant::getNullValue(structType), storage);

    auto namesIt = structFields.find(valueType.structName);
    auto typesIt = structFieldTypes.find(valueType.structName);
    if (namesIt == structFields.end() || typesIt == structFieldTypes.end()) {
        throw b::CompilerException("Unknown struct type '" + valueType.structName + "'");
    }

    std::unordered_set<std::string> initialized;
    for (const auto& field : node->fields) {
        if (!initialized.insert(field.first).second) {
            throw b::CompilerException("Field '" + field.first + "' is initialized twice in 'new " +
                                       valueType.structName + "'");
        }
        size_t index = namesIt->second.size();
        for (size_t i = 0; i < namesIt->second.size(); ++i) {
            if (namesIt->second[i] == field.first) {
                index = i;
                break;
            }
        }
        if (index == namesIt->second.size()) {
            throw b::CompilerException("Struct '" + valueType.structName + "' has no field '" +
                                       field.first + "'");
        }

        field.second->accept(this);
        llvm::Value* value = lastValue;
        llvm::Value* slot = builder->CreateStructGEP(
            llvm::cast<llvm::StructType>(structType), storage, static_cast<unsigned>(index));
        builder->CreateStore(coerceValue(value, arcTypeToLLVM(typesIt->second[field.first])), slot);
    }

    lastValue = storage;
}

void CodeGenerator::visit(b::ast::Literal* node) {
    switch (node->kind) {
        case b::ast::Literal::Kind::INTEGER: {
            lastValue = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context),
                                               parseIntLiteral(node->value), true);
            break;
        }
        case b::ast::Literal::Kind::FLOAT: {

            lastValue = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context),
                                              parseFloatLiteral(node->value));
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
        case b::ast::Literal::Kind::NONE: {
            lastValue = llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(llvm::PointerType::get(*context, 0)));
            break;
        }
    }
}

void CodeGenerator::visit(b::ast::Identifier* node) {

    if (variables.find(node->name) == variables.end()) {
        auto globalIt = globalVariables.find(node->name);
        if (globalIt != globalVariables.end()) {
            llvm::GlobalVariable* gv = globalIt->second;
            lastValue = builder->CreateLoad(gv->getValueType(), gv);
            return;
        }

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

    if (node->isMoveSource) {
        auto flagIt = dropFlags.find(node->name);
        if (flagIt != dropFlags.end()) {
            builder->CreateStore(llvm::ConstantInt::getFalse(*context), flagIt->second);
        }
    }
}

void CodeGenerator::emitShortCircuit(b::ast::BinaryOp* node) {
    bool isAnd = node->op == b::ast::BinaryOp::Operator::LOGICAL_AND;

    node->left->accept(this);
    llvm::Value* lhs = toBoolCondition(lastValue);
    llvm::BasicBlock* entryBlock = builder->GetInsertBlock();

    llvm::BasicBlock* rhsBlock =
        llvm::BasicBlock::Create(*context, isAnd ? "and.rhs" : "or.rhs", currentFunction);
    llvm::BasicBlock* endBlock =
        llvm::BasicBlock::Create(*context, isAnd ? "and.end" : "or.end", currentFunction);

    if (isAnd) {
        builder->CreateCondBr(lhs, rhsBlock, endBlock);
    } else {
        builder->CreateCondBr(lhs, endBlock, rhsBlock);
    }

    builder->SetInsertPoint(rhsBlock);
    node->right->accept(this);
    llvm::Value* rhs = toBoolCondition(lastValue);

    llvm::BasicBlock* rhsExit = builder->GetInsertBlock();
    builder->CreateBr(endBlock);

    builder->SetInsertPoint(endBlock);
    llvm::PHINode* phi = builder->CreatePHI(llvm::Type::getInt1Ty(*context), 2);
    phi->addIncoming(llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), isAnd ? 0 : 1),
                     entryBlock);
    phi->addIncoming(rhs, rhsExit);
    lastValue = phi;
}

llvm::Value* CodeGenerator::emitPointerArithmetic(b::ast::BinaryOp* node, llvm::Value* lhs,
                                                  llvm::Value* rhs,
                                                  const b::ast::Type& leftType,
                                                  const b::ast::Type& rightType) {
    bool leftIsPointer = lhs->getType()->isPointerTy();
    bool rightIsPointer = rhs->getType()->isPointerTy();
    bool subtract = node->op == b::ast::BinaryOp::Operator::MINUS;

    if (leftIsPointer && rightIsPointer) {
        if (!subtract) {
            throw b::CompilerException("Two pointers cannot be added; only p - q is defined");
        }
        if (leftType.pointerLevel != rightType.pointerLevel ||
            b::ast::typeToString(derefType(leftType)) != b::ast::typeToString(derefType(rightType))) {
            throw b::CompilerException("Cannot subtract '" + b::ast::typeToString(rightType) +
                                       "' from '" + b::ast::typeToString(leftType) + "'");
        }
        llvm::Type* elementType = arcTypeToLLVM(derefType(leftType));
        if (!elementType->isSized()) {
            throw b::CompilerException("Cannot do pointer arithmetic on an incomplete type");
        }

        return builder->CreateIntCast(builder->CreatePtrDiff(elementType, lhs, rhs),
                                      llvm::Type::getInt32Ty(*context), true);
    }

    llvm::Value* pointer = leftIsPointer ? lhs : rhs;
    llvm::Value* offset = leftIsPointer ? rhs : lhs;
    const b::ast::Type& pointerType = leftIsPointer ? leftType : rightType;

    if (!offset->getType()->isIntegerTy()) {
        throw b::CompilerException("A pointer can only be offset by an integer");
    }
    if (subtract && !leftIsPointer) {
        throw b::CompilerException("Cannot subtract a pointer from an integer");
    }

    llvm::Type* elementType = arcTypeToLLVM(derefType(pointerType));
    if (!elementType->isSized()) {
        throw b::CompilerException("Cannot do pointer arithmetic on an incomplete type");
    }

    offset = builder->CreateIntCast(offset, llvm::Type::getInt64Ty(*context), true);
    if (subtract) {
        offset = builder->CreateNeg(offset);
    }
    return builder->CreateGEP(elementType, pointer, offset);
}

void CodeGenerator::visit(b::ast::BinaryOp* node) {
    if (node->op == b::ast::BinaryOp::Operator::ASSIGN) {
        std::string targetEnum = enumTypeOf(node->left.get());
        std::string sourceEnum = enumTypeOf(node->right.get());
        if (targetEnum != sourceEnum) {
            if (targetEnum.empty()) {
                throw b::CompilerException("Cannot assign enum '" + sourceEnum +
                                           "' to a non-enum target; cast explicitly with (int)");
            }
            if (sourceEnum.empty()) {
                throw b::CompilerException("Cannot assign a non-enum value to enum type '" + targetEnum +
                                           "'; cast explicitly with (" + targetEnum + ")");
            }
            throw b::CompilerException("Cannot assign enum '" + sourceEnum + "' to enum '" +
                                       targetEnum + "'");
        }

        node->right->accept(this);
        llvm::Value* rhsValue = lastValue;

        if (auto* ident = dynamic_cast<b::ast::Identifier*>(node->left.get())) {
            if (constVariables.count(ident->name)) {
                throw b::CompilerException("Cannot assign to const variable: " + ident->name);
            }
        }

        b::ast::Type targetType;
        llvm::Value* targetPtr = addressOf(node->left.get(), &targetType);
        llvm::Type* wantedType = arcTypeToLLVM(targetType);
        b::ast::Type sourceType;
        if (inferType(node->right.get(), sourceType) && !assignableFrom(targetType, sourceType)) {
            throw b::CompilerException("Cannot assign '" + b::ast::typeToString(sourceType) +
                                       "' to '" + b::ast::typeToString(targetType) + "'");
        }
        rhsValue = coerceValue(rhsValue, wantedType);
        if (rhsValue->getType() != wantedType) {
            throw b::CompilerException("Cannot assign this value to a target of type '" +
                                       b::ast::typeToString(targetType) + "'");
        }

        if (targetType.isOwned() && !dynamic_cast<b::ast::Identifier*>(node->left.get())) {
            llvm::Type* ptrType = llvm::PointerType::get(*context, 0);
            llvm::Value* previous = builder->CreateLoad(ptrType, targetPtr);
            llvm::Value* occupied = builder->CreateICmpNE(
                previous, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrType)));
            llvm::BasicBlock* release =
                llvm::BasicBlock::Create(*context, "field.drop", currentFunction);
            llvm::BasicBlock* after =
                llvm::BasicBlock::Create(*context, "field.set", currentFunction);
            builder->CreateCondBr(occupied, release, after);
            builder->SetInsertPoint(release);
            if (targetType.slice) {
                emitSliceElementDrops(previous, targetType);
            } else {
                builder->CreateCall(dropGlueFor(targetType.structName), {previous});
            }
            builder->CreateCall(module->getFunction("b_free"), {previous});
            builder->CreateBr(after);
            builder->SetInsertPoint(after);
        }

        if (targetType.isOwned()) {
            if (auto* target = dynamic_cast<b::ast::Identifier*>(node->left.get())) {
                auto flagIt = dropFlags.find(target->name);
                if (flagIt != dropFlags.end()) {
                    for (auto scope = ownedScopes.rbegin(); scope != ownedScopes.rend(); ++scope) {
                        bool found = false;
                        for (const auto& owned : *scope) {
                            if (owned.flag == flagIt->second) {
                                emitDrop(owned);
                                found = true;
                                break;
                            }
                        }
                        if (found) {
                            break;
                        }
                    }
                    builder->CreateStore(llvm::ConstantInt::getTrue(*context), flagIt->second);
                }
            }
        }

        builder->CreateStore(rhsValue, targetPtr);
        lastValue = rhsValue;
        return;
    }

    checkEnumOperands(node);

    if (node->op == b::ast::BinaryOp::Operator::LOGICAL_AND ||
        node->op == b::ast::BinaryOp::Operator::LOGICAL_OR) {
        emitShortCircuit(node);
        return;
    }

    b::ast::Type leftArcType;
    b::ast::Type rightArcType;
    bool leftKnown = inferType(node->left.get(), leftArcType);
    bool rightKnown = inferType(node->right.get(), rightArcType);

    node->left->accept(this);
    llvm::Value* lhs = lastValue;

    node->right->accept(this);
    llvm::Value* rhs = lastValue;

    if ((node->op == b::ast::BinaryOp::Operator::PLUS ||
         node->op == b::ast::BinaryOp::Operator::MINUS) &&
        (lhs->getType()->isPointerTy() || rhs->getType()->isPointerTy())) {
        if (!leftKnown || !rightKnown) {
            throw b::CompilerException("Cannot determine the pointed-to type for pointer arithmetic");
        }
        lastValue = emitPointerArithmetic(node, lhs, rhs, leftArcType, rightArcType);
        return;
    }

    if ((node->op == b::ast::BinaryOp::Operator::DIVIDE ||
         node->op == b::ast::BinaryOp::Operator::MODULO)) {
        if (auto* divisor = llvm::dyn_cast<llvm::ConstantInt>(rhs)) {
            if (divisor->isZero()) {
                throw b::CompilerException(
                    node->op == b::ast::BinaryOp::Operator::DIVIDE
                        ? "Division by zero"
                        : "Remainder by zero");
            }
        }
    }

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
        case b::ast::BinaryOp::Operator::SHIFT_LEFT:
            if (isFloat) {
                throw b::CompilerException("Shift operators require integer operands");
            }
            lastValue = builder->CreateShl(lhs, rhs);
            break;
        case b::ast::BinaryOp::Operator::SHIFT_RIGHT:
            if (isFloat) {
                throw b::CompilerException("Shift operators require integer operands");
            }
            lastValue = builder->CreateAShr(lhs, rhs);
            break;
        default:
            throw std::runtime_error("Unknown binary operator");
    }
}

void CodeGenerator::visit(b::ast::UnaryOp* node) {
    if (node->op == b::ast::UnaryOp::Operator::DEREF) {
        b::ast::Type valueType;
        llvm::Value* pointer = addressOf(node, &valueType);
        lastValue = builder->CreateLoad(arcTypeToLLVM(valueType), pointer);
        return;
    }

    if (node->op == b::ast::UnaryOp::Operator::ADDRESS_OF) {
        b::ast::Type operandType;
        if (inferType(node->operand.get(), operandType) &&
            (operandType.isOwned() || operandType.isBorrow() || operandType.slice)) {
            node->operand->accept(this);
            return;
        }
        lastValue = addressOf(node->operand.get(), nullptr);
        return;
    }

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
        default:
            break;
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

            lastValue = sourceType->isIntegerTy(1)
                            ? builder->CreateZExt(value, targetLLVMType)
                            : builder->CreateSExt(value, targetLLVMType);
        } else {
            lastValue = builder->CreateTrunc(value, targetLLVMType);
        }
    } else if (sourceType->isIntegerTy() && targetLLVMType->isFloatingPointTy()) {
        lastValue = sourceType->isIntegerTy(1)
                        ? builder->CreateUIToFP(value, targetLLVMType)
                        : builder->CreateSIToFP(value, targetLLVMType);
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

    if (node->isIndirect()) {
        b::ast::Type calleeType;
        if (!inferType(node->callee.get(), calleeType) || !calleeType.isFunctionPointer()) {
            throw b::CompilerException("This expression is not a function pointer, so it cannot be called");
        }
        auto typedefIt = funcPointerTypedefs.find(calleeType.funcPointerTypedefName);
        if (typedefIt == funcPointerTypedefs.end()) {
            throw b::CompilerException("Unknown function pointer type '" +
                                       calleeType.funcPointerTypedefName + "'");
        }
        llvm::FunctionType* indirectType = typedefIt->second;

        node->callee->accept(this);
        llvm::Value* target = lastValue;

        if (node->arguments.size() != indirectType->getNumParams()) {
            throw b::CompilerException("'" + calleeType.funcPointerTypedefName + "' expects " +
                                       std::to_string(indirectType->getNumParams()) +
                                       " argument(s), got " + std::to_string(node->arguments.size()));
        }

        std::vector<llvm::Value*> indirectArgs;
        for (size_t i = 0; i < node->arguments.size(); ++i) {
            node->arguments[i]->accept(this);
            indirectArgs.push_back(coerceValue(lastValue, indirectType->getParamType(i)));
        }

        lastValue = builder->CreateCall(indirectType, target, indirectArgs);
        return;
    }

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
            } else if (argType->isIntegerTy(8)) {

                format += "%c";
                argValue = builder->CreateSExt(argValue, llvm::Type::getInt32Ty(*context));
            } else if (argType->isIntegerTy()) {
                format += "%d";
                if (argType->getIntegerBitWidth() < 32) {
                    argValue = builder->CreateSExt(argValue, llvm::Type::getInt32Ty(*context));
                }
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

    if (node->functionName == "len") {
        if (node->arguments.size() != 1) {
            throw b::CompilerException("len expects exactly 1 argument");
        }
        b::ast::Type argumentType;
        if (!inferType(node->arguments[0].get(), argumentType) || !argumentType.slice) {
            throw b::CompilerException("len expects a slice");
        }
        if (argumentType.fixedLength >= 0) {
            lastValue = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context),
                                               argumentType.fixedLength);
            return;
        }
        node->arguments[0]->accept(this);
        lastValue = builder->CreateIntCast(
            builder->CreateCall(module->getFunction("b_len"), {lastValue}),
            llvm::Type::getInt32Ty(*context), true);
        return;
    }

    if (node->functionName == "itoa") {
        if (node->arguments.size() != 1) {
            throw std::runtime_error("itoa expects exactly 1 argument");
        }

        llvm::Function* allocator = module->getFunction("b_alloc");
        llvm::Function* sprintfFunc = module->getFunction("sprintf");
        if (!allocator || !sprintfFunc) {
            throw b::CompilerException("Internal error: itoa needs the allocator runtime");
        }

        node->arguments[0]->accept(this);
        llvm::Value* intValue = lastValue;
        if (!intValue->getType()->isIntegerTy(32)) {
            intValue = builder->CreateIntCast(intValue, llvm::Type::getInt32Ty(*context), true);
        }

        llvm::Value* bufSize = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 12);
        llvm::Value* buffer = builder->CreateCall(allocator, {bufSize});

        llvm::Value* formatStr = builder->CreateGlobalString("%d");
        builder->CreateCall(sprintfFunc, {buffer, formatStr, intValue});

        lastValue = buffer;
        return;
    }

    std::string calleeName = node->functionName;

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
        func = module->getFunction(calleeName);
        if (!func) {
            throw b::CompilerException("Undefined function: " + node->functionName);
        }
        funcType = func->getFunctionType();
        calleeValue = func;

        auto paramIt = functionParamTypes.find(node->functionName);
        if (paramIt != functionParamTypes.end()) {
            if (paramIt->second.size() != node->arguments.size()) {
                throw b::CompilerException(node->functionName + " expects " +
                                           std::to_string(paramIt->second.size()) +
                                           " argument(s), got " +
                                           std::to_string(node->arguments.size()));
            }
            for (size_t i = 0; i < node->arguments.size(); ++i) {
                checkEnumCompatible(paramIt->second[i], node->arguments[i].get(),
                                    "argument " + std::to_string(i + 1) + " of " + node->functionName);
                b::ast::Type argumentType;
                if (inferType(node->arguments[i].get(), argumentType) &&
                    !assignableFrom(paramIt->second[i], argumentType)) {
                    throw b::CompilerException(
                        "Argument " + std::to_string(i + 1) + " of " + node->functionName +
                        " expects '" + b::ast::typeToString(paramIt->second[i]) + "' but got '" +
                        b::ast::typeToString(argumentType) + "'");
                }
            }
        }
    }

    std::vector<llvm::Value*> args;

    for (size_t i = 0; i < node->arguments.size(); ++i) {
        node->arguments[i]->accept(this);
        llvm::Value* argValue = lastValue;

        if (i < funcType->getNumParams()) {
            llvm::Type* paramType = funcType->getParamType(i);
            if (argValue->getType() != paramType) {
                argValue = coerceValue(argValue, paramType);
                if (argValue->getType() != paramType) {
                    throw b::CompilerException(
                        "Argument " + std::to_string(i + 1) + " of " + node->functionName +
                        " has the wrong type and cannot be converted implicitly");
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

bool CodeGenerator::inferType(b::ast::Expression* expr, b::ast::Type& outType) {
    if (auto* literal = dynamic_cast<b::ast::Literal*>(expr)) {
        b::ast::Type type;
        type.pointerLevel = 0;
        switch (literal->kind) {
            case b::ast::Literal::Kind::INTEGER:
                type.base = b::ast::PrimitiveType::INT;
                type.enumName = literal->enumName;
                break;
            case b::ast::Literal::Kind::FLOAT:
                type.base = b::ast::PrimitiveType::DOUBLE;
                break;
            case b::ast::Literal::Kind::STRING:
                type.base = b::ast::PrimitiveType::CHAR;
                type.pointerLevel = 1;
                break;
            case b::ast::Literal::Kind::BOOLEAN:
                type.base = b::ast::PrimitiveType::BOOL;
                break;
            case b::ast::Literal::Kind::NONE:
                type.base = b::ast::PrimitiveType::VOID;
                type.pointerLevel = 1;
                type.optional = true;
                break;
        }
        outType = type;
        return true;
    }

    if (dynamic_cast<b::ast::SizeofExpr*>(expr)) {
        outType = b::ast::Type();
        outType.base = b::ast::PrimitiveType::INT;
        return true;
    }

    if (auto* allocation = dynamic_cast<b::ast::NewExpr*>(expr)) {
        outType = allocation->type;
        return true;
    }

    if (auto* allocation = dynamic_cast<b::ast::NewSliceExpr*>(expr)) {
        outType = allocation->type;
        return true;
    }

    if (auto* ident = dynamic_cast<b::ast::Identifier*>(expr)) {
        auto it = arcVariableTypes.find(ident->name);
        if (it != arcVariableTypes.end()) {
            outType = it->second;
            return true;
        }
        return false;
    }

    if (auto* cast = dynamic_cast<b::ast::CastExpr*>(expr)) {
        outType = cast->targetType;
        return true;
    }

    if (auto* call = dynamic_cast<b::ast::FunctionCall*>(expr)) {
        if (call->isIndirect()) {
            b::ast::Type calleeType;
            if (!inferType(call->callee.get(), calleeType) || !calleeType.isFunctionPointer()) {
                return false;
            }
            auto returnIt = funcPointerReturnTypes.find(calleeType.funcPointerTypedefName);
            if (returnIt == funcPointerReturnTypes.end()) {
                return false;
            }
            outType = returnIt->second;
            return true;
        }
        auto it = functionReturnTypes.find(call->functionName);
        if (it != functionReturnTypes.end()) {
            outType = it->second;
            return true;
        }
        return false;
    }

    if (auto* member = dynamic_cast<b::ast::MemberAccess*>(expr)) {
        b::ast::Type objectType;
        if (!inferType(member->object.get(), objectType) || objectType.structName.empty()) {
            return false;
        }
        auto structIt = structFieldTypes.find(objectType.structName);
        if (structIt == structFieldTypes.end()) {
            return false;
        }
        auto fieldIt = structIt->second.find(member->member);
        if (fieldIt == structIt->second.end()) {
            return false;
        }
        outType = fieldIt->second;
        return true;
    }

    if (auto* array = dynamic_cast<b::ast::ArrayAccess*>(expr)) {
        b::ast::Type arrayType;
        if (!inferType(array->array.get(), arrayType) || arrayType.pointerLevel == 0) {
            return false;
        }
        outType = arrayType.slice ? elementTypeOf(arrayType) : derefType(arrayType);
        return true;
    }

    if (auto* unary = dynamic_cast<b::ast::UnaryOp*>(expr)) {
        b::ast::Type operandType;
        switch (unary->op) {
            case b::ast::UnaryOp::Operator::DEREF:
                if (!inferType(unary->operand.get(), operandType) || operandType.pointerLevel == 0) {
                    return false;
                }
                outType = derefType(operandType);
                return true;
            case b::ast::UnaryOp::Operator::ADDRESS_OF:
                if (!inferType(unary->operand.get(), operandType)) {
                    return false;
                }
                outType = operandType;
                if (operandType.isOwned() || operandType.isBorrow() || operandType.slice) {
                    outType.ownership = unary->mutableBorrow ? b::ast::Ownership::MutBorrow
                                                             : b::ast::Ownership::SharedBorrow;
                } else {
                    outType.ownership = unary->mutableBorrow ? b::ast::Ownership::MutBorrow
                                                             : b::ast::Ownership::SharedBorrow;
                    outType.pointerLevel++;
                }
                return true;
            case b::ast::UnaryOp::Operator::NOT:
                outType = b::ast::Type();
                outType.base = b::ast::PrimitiveType::BOOL;
                return true;
            default:
                return inferType(unary->operand.get(), outType);
        }
    }

    if (auto* binary = dynamic_cast<b::ast::BinaryOp*>(expr)) {
        switch (binary->op) {
            case b::ast::BinaryOp::Operator::EQUAL:
            case b::ast::BinaryOp::Operator::NOT_EQUAL:
            case b::ast::BinaryOp::Operator::LESS:
            case b::ast::BinaryOp::Operator::LESS_EQUAL:
            case b::ast::BinaryOp::Operator::GREATER:
            case b::ast::BinaryOp::Operator::GREATER_EQUAL:
            case b::ast::BinaryOp::Operator::LOGICAL_AND:
            case b::ast::BinaryOp::Operator::LOGICAL_OR:
                outType = b::ast::Type();
                outType.base = b::ast::PrimitiveType::BOOL;
                return true;
            default:
                return inferType(binary->left.get(), outType);
        }
    }

    return false;
}

llvm::Value* CodeGenerator::addressOf(b::ast::Expression* expr, b::ast::Type* outType) {
    if (auto* ident = dynamic_cast<b::ast::Identifier*>(expr)) {
        b::ast::Type type;
        if (outType) {
            if (!inferType(expr, type)) {
                throw b::CompilerException("Unknown variable: " + ident->name);
            }
            *outType = type;
        }
        return getVariable(ident->name);
    }

    if (auto* member = dynamic_cast<b::ast::MemberAccess*>(expr)) {
        b::ast::Type objectType;
        if (!inferType(member->object.get(), objectType)) {
            throw b::CompilerException("Cannot determine the type of the value before '." +
                                       member->member + "'");
        }
        if (objectType.structName.empty()) {
            throw b::CompilerException("'" + b::ast::typeToString(objectType) +
                                       "' is not a struct, so it has no field '" + member->member + "'");
        }
        if (objectType.optional) {
            throw b::CompilerException("Cannot reach field '" + member->member + "' through '" +
                                       b::ast::typeToString(objectType) +
                                       "' without unwrapping it with 'if some'");
        }
        if (objectType.pointerLevel > 1) {
            throw b::CompilerException("Cannot access field '" + member->member +
                                       "' through a multi-level pointer");
        }

        llvm::Value* basePtr = nullptr;
        if (objectType.pointerLevel == 0) {
            basePtr = addressOf(member->object.get(), nullptr);
        } else {
            member->object->accept(this);
            basePtr = lastValue;
        }

        auto structTypeIt = structTypes.find(objectType.structName);
        auto fieldNamesIt = structFields.find(objectType.structName);
        if (structTypeIt == structTypes.end() || fieldNamesIt == structFields.end()) {
            throw b::CompilerException("Unknown struct type: " + objectType.structName);
        }

        unsigned fieldIndex = 0;
        bool found = false;
        for (unsigned i = 0; i < fieldNamesIt->second.size(); ++i) {
            if (fieldNamesIt->second[i] == member->member) {
                fieldIndex = i;
                found = true;
                break;
            }
        }
        if (!found) {
            throw b::CompilerException("Struct '" + objectType.structName + "' has no field '" +
                                       member->member + "'");
        }

        if (outType) {
            *outType = structFieldTypes[objectType.structName][member->member];
        }
        return builder->CreateStructGEP(structTypeIt->second, basePtr, fieldIndex);
    }

    if (auto* array = dynamic_cast<b::ast::ArrayAccess*>(expr)) {
        b::ast::Type arrayType;
        if (!inferType(array->array.get(), arrayType)) {
            throw b::CompilerException("Cannot determine the element type of this index expression");
        }
        if (arrayType.optional) {
            throw b::CompilerException("Cannot index '" + b::ast::typeToString(arrayType) +
                                       "' without unwrapping it with 'if some'");
        }
        if (arrayType.pointerLevel == 0) {
            throw b::CompilerException("Cannot index a value of type '" +
                                       b::ast::typeToString(arrayType) + "'");
        }

        array->array->accept(this);
        llvm::Value* basePtr = lastValue;

        array->index->accept(this);
        llvm::Value* indexValue = lastValue;

        b::ast::Type elementType = arrayType.slice ? elementTypeOf(arrayType) : derefType(arrayType);
        if (outType) {
            *outType = elementType;
        }

        if (arrayType.slice) {
            llvm::Value* wide =
                builder->CreateIntCast(indexValue, llvm::Type::getInt64Ty(*context), true);
            bool proven = false;
            if (arrayType.fixedLength >= 0) {
                if (auto* constant = llvm::dyn_cast<llvm::ConstantInt>(wide)) {
                    int64_t at = constant->getSExtValue();
                    if (at < 0 || at >= arrayType.fixedLength) {
                        throw b::CompilerException("Index " + std::to_string(at) +
                                                   " is outside '" +
                                                   b::ast::typeToString(arrayType) + "'");
                    }
                    proven = true;
                }
            }
            if (!proven) {
                llvm::Value* length = nullptr;
                if (arrayType.fixedLength >= 0) {
                    length = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context),
                                                    arrayType.fixedLength);
                } else {
                    length = builder->CreateCall(module->getFunction("b_len"), {basePtr});
                }
                emitBoundsCheck(wide, length);
            }
            indexValue = wide;
        }

        return builder->CreateGEP(arcTypeToLLVM(elementType), basePtr, indexValue);
    }

    if (auto* unary = dynamic_cast<b::ast::UnaryOp*>(expr)) {
        if (unary->op == b::ast::UnaryOp::Operator::DEREF) {
            b::ast::Type operandType;
            if (!inferType(unary->operand.get(), operandType)) {
                throw b::CompilerException("Cannot determine the type of the dereferenced value");
            }
            if (operandType.pointerLevel == 0) {
                throw b::CompilerException("Cannot dereference a value of type '" +
                                           b::ast::typeToString(operandType) + "'");
            }

            unary->operand->accept(this);
            llvm::Value* pointerValue = lastValue;

            if (outType) {
                *outType = derefType(operandType);
            }
            return pointerValue;
        }
    }

    throw b::CompilerException("Expression cannot be used as an assignment target");
}

llvm::Value* CodeGenerator::coerceValue(llvm::Value* value, llvm::Type* targetType) {
    llvm::Type* sourceType = value->getType();
    if (sourceType == targetType) {
        return value;
    }

    if (sourceType->isIntegerTy() && targetType->isIntegerTy()) {
        return builder->CreateIntCast(value, targetType, !sourceType->isIntegerTy(1));
    }
    if (sourceType->isIntegerTy() && targetType->isFloatingPointTy()) {
        return sourceType->isIntegerTy(1) ? builder->CreateUIToFP(value, targetType)
                                          : builder->CreateSIToFP(value, targetType);
    }
    if (sourceType->isFloatingPointTy() && targetType->isIntegerTy()) {
        return builder->CreateFPToSI(value, targetType);
    }
    if (sourceType->isFloatingPointTy() && targetType->isFloatingPointTy()) {
        return builder->CreateFPCast(value, targetType);
    }
    if (sourceType->isIntegerTy() && targetType->isPointerTy()) {
        if (auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(value)) {
            if (constInt->isZero()) {
                return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(targetType));
            }
        }
    }

    return value;
}

void CodeGenerator::visit(b::ast::MemberAccess* node) {
    b::ast::Type fieldType;
    llvm::Value* fieldPtr = addressOf(node, &fieldType);
    lastValue = builder->CreateLoad(arcTypeToLLVM(fieldType), fieldPtr);
}

void CodeGenerator::visit(b::ast::ArrayAccess* node) {
    b::ast::Type elementType;
    llvm::Value* elementPtr = addressOf(node, &elementType);
    lastValue = builder->CreateLoad(arcTypeToLLVM(elementType), elementPtr);
}

void CodeGenerator::visit(b::ast::VariableDecl* node) {
    llvm::Type* type = arcTypeToLLVM(node->type);

    if (node->arraySize > 0) {
        b::ast::Type sliceType = node->type;
        sliceType.slice = true;
        sliceType.fixedLength = node->arraySize;

        b::ast::Type elementType = elementTypeOf(sliceType);
        llvm::Type* elementLLVMType = arcTypeToLLVM(elementType);
        llvm::Type* i64Type = llvm::Type::getInt64Ty(*context);

        llvm::StructType* backing = llvm::StructType::get(
            *context, {i64Type, i64Type, llvm::ArrayType::get(elementLLVMType, node->arraySize)});
        llvm::Value* storage = createEntryAlloca(backing, node->name + ".store");

        llvm::Value* lengthSlot = builder->CreateStructGEP(backing, storage, 1);
        builder->CreateStore(llvm::ConstantInt::get(i64Type, node->arraySize), lengthSlot);

        llvm::Value* data = builder->CreateStructGEP(backing, storage, 2);
        llvm::AllocaInst* pointerSlot = createEntryAlloca(type, node->name);
        builder->CreateStore(data, pointerSlot);

        setVariable(node->name, pointerSlot);
        variableTypes[node->name] = type;
        arcVariableTypes[node->name] = sliceType;
        return;
    }

    llvm::AllocaInst* alloca = createEntryAlloca(type, node->name);

    if (node->initializer) {
        node->initializer->accept(this);
        checkEnumCompatible(node->type, node->initializer.get(), "variable '" + node->name + "'");
        b::ast::Type sourceType;
        if (inferType(node->initializer.get(), sourceType) &&
            !assignableFrom(node->type, sourceType)) {
            throw b::CompilerException("Cannot initialize '" + node->name + "' of type '" +
                                       b::ast::typeToString(node->type) + "' with '" +
                                       b::ast::typeToString(sourceType) + "'");
        }
        builder->CreateStore(coerceValue(lastValue, type), alloca);
    } else if (node->type.isOwned()) {
        builder->CreateStore(llvm::Constant::getNullValue(type), alloca);
    }

    if (node->type.isOwned()) {
        registerOwnedLocal(node->name, node->type, alloca, node->initializer != nullptr);
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
        if (currentReturnType.isVoid()) {
            throw b::CompilerException("Cannot return a value from a void function");
        }
        checkEnumCompatible(currentReturnType, node->value.get(), "return value");
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

        emitScopeDrops(0);
        builder->CreateRet(retVal);
    } else {
        emitScopeDrops(0);
        builder->CreateRetVoid();
    }
}

void CodeGenerator::visit(b::ast::ExpressionStmt* node) {
    node->expression->accept(this);
}

void CodeGenerator::visit(b::ast::Block* node) {
    pushScope();
    ownedScopes.emplace_back();
    std::unordered_set<std::string> outerConsts = constVariables;
    for (const auto& stmt : node->statements) {
        if (blockIsTerminated()) {
            break;
        }
        stmt->accept(this);
    }
    if (!blockIsTerminated()) {
        emitScopeDrops(ownedScopes.size() - 1);
    }
    ownedScopes.pop_back();
    constVariables = std::move(outerConsts);
    popScope();
}

void CodeGenerator::visit(b::ast::IfSomeStmt* node) {
    b::ast::Type sourceType;
    if (!inferType(node->source.get(), sourceType) || !sourceType.optional) {
        throw b::CompilerException("'if some' needs an optional value to unwrap");
    }

    node->source->accept(this);
    llvm::Value* candidate = lastValue;
    llvm::Type* ptrType = llvm::PointerType::get(*context, 0);
    llvm::Value* present = builder->CreateICmpNE(
        candidate, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrType)));

    llvm::BasicBlock* someBlock = llvm::BasicBlock::Create(*context, "some", currentFunction);
    llvm::BasicBlock* noneBlock = nullptr;
    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(*context, "someend", currentFunction);

    if (node->elseBranch) {
        noneBlock = llvm::BasicBlock::Create(*context, "none", currentFunction);
        builder->CreateCondBr(present, someBlock, noneBlock);
    } else {
        builder->CreateCondBr(present, someBlock, mergeBlock);
    }

    builder->SetInsertPoint(someBlock);
    pushScope();
    b::ast::Type boundType = sourceType;
    boundType.optional = false;
    boundType.ownership = node->mutableBinding ? b::ast::Ownership::MutBorrow
                                               : b::ast::Ownership::SharedBorrow;
    llvm::AllocaInst* slot = createEntryAlloca(ptrType, node->binding);
    builder->CreateStore(candidate, slot);
    setVariable(node->binding, slot);
    variableTypes[node->binding] = ptrType;
    arcVariableTypes[node->binding] = boundType;
    node->thenBranch->accept(this);
    popScope();
    if (!blockIsTerminated()) {
        builder->CreateBr(mergeBlock);
    }

    if (noneBlock) {
        builder->SetInsertPoint(noneBlock);
        node->elseBranch->accept(this);
        if (!blockIsTerminated()) {
            builder->CreateBr(mergeBlock);
        }
    }

    builder->SetInsertPoint(mergeBlock);
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
    loopOwnedDepth.push_back(ownedScopes.size());

    builder->SetInsertPoint(bodyBlock);
    node->body->accept(this);
    if (!blockIsTerminated()) {
        builder->CreateBr(incrementBlock);
    }

    loopOwnedDepth.pop_back();
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
    loopOwnedDepth.push_back(ownedScopes.size());

    builder->SetInsertPoint(bodyBlock);
    node->body->accept(this);
    if (!blockIsTerminated()) {
        builder->CreateBr(condBlock);
    }

    loopOwnedDepth.pop_back();
    breakTargets.pop_back();
    continueTargets.pop_back();

    builder->SetInsertPoint(endBlock);
}

void CodeGenerator::visit(b::ast::BreakStmt* node) {
    (void)node;
    if (breakTargets.empty()) {
        throw std::runtime_error("'break' used outside of a loop");
    }
    emitScopeDrops(loopOwnedDepth.empty() ? 0 : loopOwnedDepth.back());
    builder->CreateBr(breakTargets.back());
}

void CodeGenerator::visit(b::ast::ContinueStmt* node) {
    (void)node;
    if (continueTargets.empty()) {
        throw std::runtime_error("'continue' used outside of a loop");
    }
    emitScopeDrops(loopOwnedDepth.empty() ? 0 : loopOwnedDepth.back());
    builder->CreateBr(continueTargets.back());
}

void CodeGenerator::visit(b::ast::SwitchStmt* node) {
    std::string conditionEnum = enumTypeOf(node->condition.get());
    bool hasDefault = false;
    std::unordered_set<std::string> coveredConstants;

    for (const auto& caseItem : node->cases) {
        if (caseItem.isDefault) {
            hasDefault = true;
            continue;
        }

        std::string caseEnum = enumTypeOf(caseItem.value.get());
        if (conditionEnum != caseEnum) {
            if (conditionEnum.empty()) {
                throw b::CompilerException("Cannot use a constant of enum '" + caseEnum +
                                           "' in a switch over a non-enum value");
            }
            throw b::CompilerException("Switch over enum '" + conditionEnum +
                                       "' requires case labels of that enum");
        }

        if (auto* literal = dynamic_cast<b::ast::Literal*>(caseItem.value.get())) {
            coveredConstants.insert(literal->value);
        }
    }

    if (!conditionEnum.empty() && !hasDefault) {
        auto membersIt = enumMembers.find(conditionEnum);
        if (membersIt != enumMembers.end()) {
            std::string missing;
            for (const auto& constant : membersIt->second) {
                if (!coveredConstants.count(std::to_string(constant.value))) {
                    if (!missing.empty()) {
                        missing += ", ";
                    }
                    missing += constant.name;
                }
            }
            if (!missing.empty()) {
                std::cerr << "Warning: switch over enum '" << conditionEnum
                          << "' does not handle " << missing
                          << " and has no default case" << std::endl;
            }
        }
    }

    node->condition->accept(this);
    llvm::Value* switchValue = lastValue;
    if (!switchValue->getType()->isIntegerTy()) {
        throw b::CompilerException("switch requires an integer, char, bool or enum value");
    }

    std::vector<llvm::ConstantInt*> caseValues(node->cases.size(), nullptr);
    std::unordered_set<int64_t> seenValues;
    for (size_t i = 0; i < node->cases.size(); ++i) {
        if (node->cases[i].isDefault) {
            continue;
        }
        llvm::Constant* folded = evalConstantExpr(node->cases[i].value.get(), "a case label");
        auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(folded);
        if (!constInt) {
            throw b::CompilerException("A case label must be a constant integer, char or enum constant");
        }
        constInt = castConstantInt(constInt, switchValue->getType(), true);
        if (!seenValues.insert(constInt->getSExtValue()).second) {
            throw b::CompilerException("Duplicate case label " +
                                       std::to_string(constInt->getSExtValue()) + " in switch");
        }
        caseValues[i] = constInt;
    }

    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(*context, "switch.merge", currentFunction);
    llvm::SwitchInst* switchInst = builder->CreateSwitch(switchValue, mergeBlock, node->cases.size());

    std::vector<llvm::BasicBlock*> caseBlocks;
    caseBlocks.reserve(node->cases.size());
    for (size_t i = 0; i < node->cases.size(); ++i) {
        caseBlocks.push_back(llvm::BasicBlock::Create(*context, "switch.case", currentFunction));
    }

    for (size_t i = 0; i < node->cases.size(); ++i) {
        if (node->cases[i].isDefault) {
            switchInst->setDefaultDest(caseBlocks[i]);
        } else {
            switchInst->addCase(caseValues[i], caseBlocks[i]);
        }
    }

    for (size_t i = 0; i < node->cases.size(); ++i) {
        builder->SetInsertPoint(caseBlocks[i]);
        breakTargets.push_back(mergeBlock);

        pushScope();
        for (const auto& stmt : node->cases[i].statements) {
            if (blockIsTerminated()) {
                break;
            }
            stmt->accept(this);
        }
        popScope();

        breakTargets.pop_back();

        if (!blockIsTerminated()) {
            builder->CreateBr(i + 1 < caseBlocks.size() ? caseBlocks[i + 1] : mergeBlock);
        }
    }

    builder->SetInsertPoint(mergeBlock);
}

void CodeGenerator::visit(b::ast::FunctionDecl* node) {
    llvm::Function* function = module->getFunction(node->name);
    if (!function) {
        throw std::runtime_error("Function was not declared: " + node->name);
    }

    llvm::BasicBlock* entryBlock = llvm::BasicBlock::Create(*context, "entry", function);
    builder->SetInsertPoint(entryBlock);

    currentFunction = function;
    currentReturnType = node->returnType;
    variables.clear();
    variableTypes.clear();
    arcVariableTypes.clear();
    dropFlags.clear();
    ownedScopes.clear();
    loopOwnedDepth.clear();
    ownedScopes.emplace_back();

    constVariables = constGlobals;

    for (const auto& globalType : globalVariableTypes) {
        arcVariableTypes[globalType.first] = globalType.second;
        variableTypes[globalType.first] = arcTypeToLLVM(globalType.second);
    }

    auto argIt = function->arg_begin();
    for (const auto& param : node->parameters) {
        argIt->setName(param.name);
        llvm::Type* paramType = arcTypeToLLVM(param.type);
        llvm::AllocaInst* alloca = createEntryAlloca(paramType, param.name);
        builder->CreateStore(&*argIt, alloca);
        if (param.type.isOwned() && node->dropsType.empty()) {
            registerOwnedLocal(param.name, param.type, alloca, true);
        }
        setVariable(param.name, alloca);
        arcVariableTypes[param.name] = param.type;
        variableTypes[param.name] = paramType;
        ++argIt;
    }

    if (node->body) {
        node->body->accept(this);
    }

    if (!blockIsTerminated()) {
        llvm::BasicBlock* tail = builder->GetInsertBlock();
        if (node->returnType.isVoid()) {
            emitScopeDrops(0);
            builder->CreateRetVoid();
        } else if (tail != &function->getEntryBlock() && tail->hasNPredecessors(0)) {

            builder->CreateUnreachable();
        } else {
            throw b::CompilerException("Function '" + node->name + "' must return a value of type '" +
                                       b::ast::typeToString(node->returnType) +
                                       "' on every path, but control can reach its closing brace");
        }
    }
}

void CodeGenerator::visit(b::ast::StructDecl* node) {
    auto it = structTypes.find(node->name);
    if (it == structTypes.end()) {
        throw std::runtime_error("Unknown struct type: " + node->name);
    }

    std::vector<llvm::Type*> fieldTypes;
    std::vector<std::string> fieldNames;
    std::unordered_map<std::string, b::ast::Type> fieldTypeMap;

    for (const auto& field : node->fields) {
        if (field.type.isStruct() && field.type.pointerLevel == 0 &&
            field.type.structName == node->name) {
            throw b::CompilerException("Struct '" + node->name + "' cannot contain itself by value; use " +
                                       node->name + "* instead");
        }
        fieldTypes.push_back(arcTypeToLLVM(field.type));
        fieldNames.push_back(field.name);
        fieldTypeMap[field.name] = field.type;
    }

    it->second->setBody(fieldTypes);
    structFields[node->name] = fieldNames;
    structFieldTypes[node->name] = std::move(fieldTypeMap);
}

void CodeGenerator::checkStructCycles(b::ast::Program* program) {
    std::unordered_map<std::string, std::vector<std::string>> containedByValue;
    for (const auto& strct : program->structs) {
        std::vector<std::string> nested;
        for (const auto& field : strct->fields) {
            if (field.type.isStruct() && field.type.pointerLevel == 0) {
                nested.push_back(field.type.structName);
            }
        }
        containedByValue[strct->name] = std::move(nested);
    }

    std::unordered_set<std::string> settled;
    std::vector<std::string> path;
    std::unordered_set<std::string> onPath;

    std::function<void(const std::string&)> walk = [&](const std::string& name) {
        if (settled.count(name)) {
            return;
        }
        if (onPath.count(name)) {
            std::string chain;
            bool started = false;
            for (const auto& step : path) {
                if (step == name) started = true;
                if (started) chain += step + " -> ";
            }
            throw b::CompilerException("Struct '" + name + "' contains itself by value (" + chain +
                                       name + "); make one of the fields a pointer");
        }
        auto it = containedByValue.find(name);
        if (it == containedByValue.end()) {
            return;
        }
        onPath.insert(name);
        path.push_back(name);
        for (const auto& nested : it->second) {
            walk(nested);
        }
        path.pop_back();
        onPath.erase(name);
        settled.insert(name);
    };

    for (const auto& entry : containedByValue) {
        walk(entry.first);
    }
}

void CodeGenerator::visit(b::ast::Program* node) {
    for (const auto& enumDecl : node->enums) {
        enumMembers[enumDecl.name] = enumDecl.constants;
    }

    for (const auto& strct : node->structs) {
        if (structTypes.count(strct->name)) {
            throw b::CompilerException("Duplicate struct definition: " + strct->name);
        }
        structTypes[strct->name] = llvm::StructType::create(*context, strct->name);
    }

    checkStructCycles(node);

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
        funcPointerReturnTypes[typedefDecl.name] = typedefDecl.returnType;
    }

    for (const auto& globalVar : node->globalVariables) {
        if (globalVariables.count(globalVar.name)) {
            throw b::CompilerException("Duplicate global variable: " + globalVar.name);
        }

        llvm::Type* llvmType = arcTypeToLLVM(globalVar.type);
        llvm::Constant* initializer = nullptr;
        if (globalVar.initializer) {
            checkEnumCompatible(globalVar.type, globalVar.initializer.get(),
                                "global variable '" + globalVar.name + "'");
            initializer = evalConstantExpr(globalVar.initializer.get(),
                                           "the initializer of global '" + globalVar.name + "'");
            if (initializer->getType() != llvmType) {
                if (initializer->getType()->isIntegerTy() && llvmType->isIntegerTy()) {
                    initializer = castConstantInt(llvm::cast<llvm::ConstantInt>(initializer),
                                                  llvmType,
                                                  !initializer->getType()->isIntegerTy(1));
                } else if (initializer->getType()->isFloatingPointTy() && llvmType->isFloatingPointTy()) {
                    initializer = llvm::ConstantFP::get(
                        llvmType, llvm::cast<llvm::ConstantFP>(initializer)->getValueAPF().convertToDouble());
                } else if (auto* asInt = llvm::dyn_cast<llvm::ConstantInt>(initializer)) {
                    if (llvmType->isFloatingPointTy()) {
                        initializer = llvm::ConstantFP::get(
                            llvmType, static_cast<double>(asInt->getSExtValue()));
                    } else if (llvmType->isPointerTy() && asInt->isZero()) {
                        initializer = llvm::Constant::getNullValue(llvmType);
                    }
                }
            }
            if (initializer->getType() != llvmType) {
                throw b::CompilerException("Global '" + globalVar.name + "' of type '" +
                                           b::ast::typeToString(globalVar.type) +
                                           "' cannot be initialized with this value");
            }
        } else {
            initializer = llvm::Constant::getNullValue(llvmType);
        }

        auto gv = new llvm::GlobalVariable(*module, llvmType, globalVar.isConst,
                                           llvm::GlobalValue::InternalLinkage, initializer, globalVar.name);
        globalVariables[globalVar.name] = gv;
        globalVariableTypes[globalVar.name] = globalVar.type;
        if (globalVar.isConst) {
            constVariables.insert(globalVar.name);
            constGlobals.insert(globalVar.name);
            constGlobalValues[globalVar.name] = initializer;
        }
    }

    for (const auto& func : node->functions) {
        if (functionReturnTypes.count(func->name)) {
            throw b::CompilerException("Duplicate definition of function '" + func->name + "'");
        }
        if (module->getFunction(func->name)) {
            throw b::CompilerException("Function '" + func->name +
                                       "' collides with a built-in of the same name");
        }

        llvm::FunctionType* funcType = createFunctionType(func.get());
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, func->name, module.get());

        functionReturnTypes[func->name] = func->returnType;
        std::vector<b::ast::Type> paramTypes;
        for (const auto& param : func->parameters) {
            paramTypes.push_back(param.type);
        }
        functionParamTypes[func->name] = std::move(paramTypes);
    }

    for (const auto& func : node->functions) {
        if (func->dropsType.empty()) {
            continue;
        }
        if (!structTypes.count(func->dropsType)) {
            throw b::CompilerException("'drop " + func->dropsType +
                                       "' names a type that is not a struct");
        }
        if (userDropFunctions.count(func->dropsType)) {
            throw b::CompilerException("Struct '" + func->dropsType +
                                       "' already has a drop function");
        }
        userDropFunctions[func->dropsType] = module->getFunction(func->name);
    }

    for (const auto& func : node->functions) {
        func->accept(this);
    }

    if (!module->getFunction("main")) {
        throw b::CompilerException("No 'main' function found");
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

class ModuleLoader {
public:
    std::vector<b::lexer::Token> load(const std::string& entryPath);
    const std::vector<std::string>& modules() const { return order; }
    const std::vector<std::string>& cycles() const { return importCycles; }
    const std::unordered_map<std::string, std::string>& sources() const { return moduleSources; }

private:
    struct ImportRequest {
        std::string path;
        int line;
    };

    std::unordered_set<std::string> loaded;
    std::unordered_set<std::string> loading;
    std::vector<std::string> order;
    std::vector<b::lexer::Token> tokens;
    std::vector<std::string> stackKeys;
    std::vector<std::string> stackNames;
    std::vector<std::string> importCycles;
    std::unordered_map<std::string, std::string> moduleSources;

    void recordCycle(const std::string& key);
    void loadFile(const std::string& path, const std::string& importedFrom, int importLine);
    static std::string resolve(const std::string& raw, const fs::path& importerDir);
    static std::string displayName(const fs::path& path);
    static std::vector<fs::path> searchRoots();
};

std::string ModuleLoader::displayName(const fs::path& path) {
    std::error_code ec;
    fs::path relative = fs::relative(path, fs::current_path(), ec);
    if (ec || relative.empty() || relative.string().rfind("..", 0) == 0) {
        return path.string();
    }
    return relative.string();
}

void ModuleLoader::recordCycle(const std::string& key) {
    auto it = std::find(stackKeys.begin(), stackKeys.end(), key);
    if (it == stackKeys.end()) {
        return;
    }

    size_t start = static_cast<size_t>(it - stackKeys.begin());
    std::string chain;
    for (size_t i = start; i < stackNames.size(); ++i) {
        chain += stackNames[i] + " -> ";
    }
    chain += displayName(key);

    if (std::find(importCycles.begin(), importCycles.end(), chain) == importCycles.end()) {
        importCycles.push_back(chain);
    }
}

std::vector<fs::path> ModuleLoader::searchRoots() {
    std::vector<fs::path> roots;

    if (const char* configured = std::getenv("B_PATH")) {
        std::string value(configured);
        size_t start = 0;
        while (start <= value.size()) {
            size_t stop = value.find(':', start);
            std::string piece =
                value.substr(start, stop == std::string::npos ? std::string::npos : stop - start);
            if (!piece.empty()) {
                roots.emplace_back(piece);
            }
            if (stop == std::string::npos) {
                break;
            }
            start = stop + 1;
        }
    }

    if (const char* home = std::getenv("HOME")) {
        roots.push_back(fs::path(home) / ".b");
    }
    if (const char* profile = std::getenv("USERPROFILE")) {
        roots.push_back(fs::path(profile) / ".b");
    }
    return roots;
}

std::string ModuleLoader::resolve(const std::string& raw, const fs::path& importerDir) {
    std::vector<fs::path> candidates;
    fs::path rawPath(raw);

    if (rawPath.is_absolute()) {
        candidates.push_back(rawPath);
    } else {
        candidates.push_back(importerDir / rawPath);
        candidates.push_back(fs::current_path() / rawPath);
        for (const auto& root : searchRoots()) {
            candidates.push_back(root / rawPath);
        }
    }

    size_t fixedCount = candidates.size();
    if (rawPath.extension() != ".b") {
        for (size_t i = 0; i < fixedCount; ++i) {
            fs::path withExtension = candidates[i];
            withExtension += ".b";
            candidates.push_back(withExtension);
        }
    }

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec)) {
            fs::path canonical = fs::weakly_canonical(candidate, ec);
            return ec ? candidate.string() : canonical.string();
        }
    }

    return "";
}

void ModuleLoader::loadFile(const std::string& path, const std::string& importedFrom, int importLine) {
    std::error_code ec;
    fs::path canonicalPath = fs::weakly_canonical(fs::path(path), ec);
    std::string key = ec ? path : canonicalPath.string();

    if (loading.count(key)) {
        recordCycle(key);
        return;
    }
    if (loaded.count(key)) {
        return;
    }
    loading.insert(key);

    std::string name = displayName(key);
    stackKeys.push_back(key);
    stackNames.push_back(name);
    std::string source;
    try {
        source = readFile(key);
    } catch (const b::CompilerException&) {
        if (importedFrom.empty()) {
            throw;
        }
        throw b::CompilerException("Cannot open module '" + path + "' imported from " +
                                   importedFrom + ":" + std::to_string(importLine));
    }

    moduleSources[name] = source;

    std::vector<b::lexer::Token> fileTokens;
    try {
        b::lexer::Lexer lexer(source);
        fileTokens = lexer.tokenize();
    } catch (const std::exception& ex) {
        throw b::CompilerException(std::string(ex.what()) + " in " + name);
    }

    std::vector<b::lexer::Token> kept;
    std::vector<ImportRequest> imports;
    int depth = 0;

    for (size_t i = 0; i < fileTokens.size(); ++i) {
        b::lexer::Token token = fileTokens[i];

        if (token.type == b::lexer::TokenType::KW_IMPORT) {
            if (depth != 0) {
                throw b::CompilerException("import must appear at the top level (" + name +
                                           ":" + std::to_string(token.line) + ")");
            }
            if (i + 2 >= fileTokens.size() ||
                fileTokens[i + 1].type != b::lexer::TokenType::STRING ||
                fileTokens[i + 2].type != b::lexer::TokenType::SEMICOLON) {
                throw b::CompilerException("Expected import \"path/to/module.b\"; in " + name +
                                           ":" + std::to_string(token.line));
            }
            imports.push_back({fileTokens[i + 1].value, fileTokens[i + 1].line});
            i += 2;
            continue;
        }

        if (token.type == b::lexer::TokenType::LBRACE) {
            depth++;
        } else if (token.type == b::lexer::TokenType::RBRACE) {
            depth--;
        }

        if (token.type == b::lexer::TokenType::EOF_TOKEN) {
            continue;
        }

        token.file = name;
        kept.push_back(std::move(token));
    }

    fs::path importerDir = fs::path(key).parent_path();
    for (const auto& request : imports) {
        std::string resolved = resolve(request.path, importerDir);
        if (resolved.empty()) {
            throw b::CompilerException(
                "Cannot find module '" + request.path + "' imported from " + name + ":" +
                std::to_string(request.line) +
                "\n  searched next to the importing file, in the working directory, and in "
                "$B_PATH and ~/.b");
        }
        loadFile(resolved, name, request.line);
    }

    tokens.insert(tokens.end(), kept.begin(), kept.end());

    stackKeys.pop_back();
    stackNames.pop_back();
    loading.erase(key);
    loaded.insert(key);
    order.push_back(name);
}

std::vector<b::lexer::Token> ModuleLoader::load(const std::string& entryPath) {
    tokens.clear();
    loaded.clear();
    loading.clear();
    order.clear();
    moduleSources.clear();
    stackKeys.clear();
    stackNames.clear();
    importCycles.clear();

    loadFile(entryPath, "", 0);
    tokens.push_back(b::lexer::Token(b::lexer::TokenType::EOF_TOKEN, "", "", 0, 0));
    return tokens;
}

namespace b::modules {

class NamespaceResolver {
public:
    std::vector<b::lexer::Token> resolve(std::vector<b::lexer::Token> input);
    const std::vector<std::string>& namespaces() const { return declared; }

private:
    struct Scope {
        std::unordered_set<std::string> names;
        std::unordered_set<std::string> children;
    };

    struct Frame {
        std::string qualified;
        size_t depth;
    };

    struct UsingEntry {
        std::string qualified;
        size_t depth;
        std::string file;
    };

    std::vector<b::lexer::Token> tokens;
    std::unordered_map<std::string, Scope> scopes;
    std::vector<std::string> declared;
    std::unordered_map<size_t, std::string> declSites;
    std::unordered_set<size_t> frozen;

    std::vector<Frame> nsStack;
    std::vector<UsingEntry> usings;
    size_t depth = 0;

    static std::string join(const std::string& scope, const std::string& name) {
        return scope.empty() ? name : scope + "::" + name;
    }
    static std::string mangle(const std::string& qualified);

    bool isType(size_t index, b::lexer::TokenType type) const {
        return index < tokens.size() && tokens[index].type == type;
    }
    bool atDeclarationLevel() const {
        return nsStack.empty() ? depth == 0 : depth == nsStack.back().depth + 1;
    }
    std::string currentScope() const {
        return nsStack.empty() ? std::string() : nsStack.back().qualified;
    }
    Scope& scopeFor(const std::string& qualified) { return scopes[qualified]; }
    const Scope* findScope(const std::string& qualified) const;

    std::string at(size_t index) const;
    size_t skipBalanced(size_t index, b::lexer::TokenType open, b::lexer::TokenType close) const;
    size_t skipToSemicolon(size_t index) const;
    std::vector<std::string> readPath(size_t& index) const;

    void declare(const std::string& name, size_t nameIndex);
    void closeNamespaces();
    size_t openNamespace(size_t index);

    void collect();
    size_t collectStruct(size_t index);
    size_t collectEnum(size_t index);
    size_t collectTypedef(size_t index);
    size_t collectFunctionOrGlobal(size_t index);
    size_t skipDropDeclaration(size_t index);
    size_t readTypeParams(size_t index, std::unordered_set<std::string>& out);
    void freezeFieldNames(size_t from, size_t to);
    void freezeMatching(size_t from, size_t to, const std::unordered_set<std::string>& names);
    void checkCollisions() const;
    void freezeInitializerFieldNames();

    std::vector<b::lexer::Token> rewrite();
    size_t applyUsing(size_t index);
    size_t emitIdentifier(size_t index, std::vector<b::lexer::Token>& out);
    std::string findBaseScope(const std::string& first, size_t index) const;
    std::string resolveFrom(const std::string& base, const std::vector<std::string>& parts,
                            size_t index) const;
    std::string resolveNamespace(const std::vector<std::string>& parts, size_t index) const;
    std::string resolveUnqualified(const std::string& name, size_t index) const;
};

std::string NamespaceResolver::mangle(const std::string& qualified) {
    std::string flat;
    flat.reserve(qualified.size());
    for (size_t i = 0; i < qualified.size(); ++i) {
        if (qualified[i] == ':' && i + 1 < qualified.size() && qualified[i + 1] == ':') {
            flat += "__";
            ++i;
        } else {
            flat += qualified[i];
        }
    }
    return flat;
}

const NamespaceResolver::Scope* NamespaceResolver::findScope(const std::string& qualified) const {
    auto it = scopes.find(qualified);
    return it == scopes.end() ? nullptr : &it->second;
}

std::string NamespaceResolver::at(size_t index) const {
    if (index >= tokens.size()) {
        return "";
    }
    const b::lexer::Token& token = tokens[index];
    std::string where = " (";
    if (!token.file.empty()) {
        where += token.file + ":";
    }
    where += "line " + std::to_string(token.line) + ")";
    return where;
}

size_t NamespaceResolver::skipBalanced(size_t index, b::lexer::TokenType open,
                                       b::lexer::TokenType close) const {
    if (!isType(index, open)) {
        return index;
    }
    size_t nesting = 0;
    for (size_t i = index; i < tokens.size(); ++i) {
        if (tokens[i].type == open) {
            ++nesting;
        } else if (tokens[i].type == close) {
            --nesting;
            if (nesting == 0) {
                return i + 1;
            }
        } else if (tokens[i].type == b::lexer::TokenType::EOF_TOKEN) {
            return i;
        }
    }
    return tokens.size();
}

size_t NamespaceResolver::skipToSemicolon(size_t index) const {
    size_t nesting = 0;
    for (size_t i = index; i < tokens.size(); ++i) {
        b::lexer::TokenType type = tokens[i].type;
        if (type == b::lexer::TokenType::LBRACE) {
            ++nesting;
        } else if (type == b::lexer::TokenType::RBRACE) {
            if (nesting == 0) {
                return i;
            }
            --nesting;
        } else if (type == b::lexer::TokenType::SEMICOLON && nesting == 0) {
            return i + 1;
        } else if (type == b::lexer::TokenType::EOF_TOKEN) {
            return i;
        }
    }
    return tokens.size();
}

std::vector<std::string> NamespaceResolver::readPath(size_t& index) const {
    std::vector<std::string> parts;
    while (isType(index, b::lexer::TokenType::IDENTIFIER)) {
        parts.push_back(tokens[index].lexeme);
        ++index;
        if (!isType(index, b::lexer::TokenType::COLON_COLON)) {
            break;
        }
        ++index;
    }
    return parts;
}

void NamespaceResolver::declare(const std::string& name, size_t nameIndex) {
    std::string scope = currentScope();
    scopeFor(scope).names.insert(name);
    if (!scope.empty()) {
        declSites[nameIndex] = mangle(join(scope, name));
    } else {

        frozen.insert(nameIndex);
    }
}

void NamespaceResolver::closeNamespaces() {
    while (!nsStack.empty() && nsStack.back().depth == depth) {
        nsStack.pop_back();
    }
}

size_t NamespaceResolver::openNamespace(size_t index) {
    size_t i = index + 1;
    std::vector<std::string> parts = readPath(i);
    if (parts.empty()) {
        throw b::CompilerException("Expected a name after 'namespace'" + at(index));
    }
    if (!isType(i, b::lexer::TokenType::LBRACE)) {
        throw b::CompilerException("Expected '{' after namespace '" + parts.back() + "'" + at(i));
    }
    ++i;

    std::string qualified = currentScope();
    for (const auto& part : parts) {
        std::string parent = qualified;
        qualified = join(parent, part);
        scopeFor(parent).children.insert(part);
        scopeFor(qualified);
        nsStack.push_back({qualified, depth});
        if (std::find(declared.begin(), declared.end(), qualified) == declared.end()) {
            declared.push_back(qualified);
        }
    }
    ++depth;
    return i;
}

size_t NamespaceResolver::readTypeParams(size_t index, std::unordered_set<std::string>& out) {
    int angle = 0;
    for (size_t i = index; i < tokens.size(); ++i) {
        b::lexer::TokenType type = tokens[i].type;
        if (type == b::lexer::TokenType::LESS) {
            ++angle;
        } else if (type == b::lexer::TokenType::GREATER) {
            if (--angle <= 0) return i + 1;
        } else if (type == b::lexer::TokenType::GREATER_GREATER) {
            angle -= 2;
            if (angle <= 0) return i + 1;
        } else if (type == b::lexer::TokenType::IDENTIFIER) {
            out.insert(tokens[i].lexeme);
            frozen.insert(i);
        } else if (type == b::lexer::TokenType::LBRACE ||
                   type == b::lexer::TokenType::SEMICOLON ||
                   type == b::lexer::TokenType::EOF_TOKEN) {
            return i;
        }
    }
    return tokens.size();
}

void NamespaceResolver::freezeFieldNames(size_t from, size_t to) {
    size_t lastIdent = tokens.size();
    int angle = 0;
    for (size_t i = from; i < to && i < tokens.size(); ++i) {
        switch (tokens[i].type) {
            case b::lexer::TokenType::LESS:
                ++angle;
                break;
            case b::lexer::TokenType::GREATER:
                if (angle > 0) --angle;
                break;
            case b::lexer::TokenType::GREATER_GREATER:
                angle = angle > 1 ? angle - 2 : 0;
                break;
            case b::lexer::TokenType::IDENTIFIER:
                if (angle == 0) lastIdent = i;
                break;
            case b::lexer::TokenType::LBRACKET:
            case b::lexer::TokenType::SEMICOLON:
                if (lastIdent < tokens.size()) frozen.insert(lastIdent);
                lastIdent = tokens.size();
                break;
            default:
                break;
        }
    }
}

void NamespaceResolver::freezeMatching(size_t from, size_t to,
                                       const std::unordered_set<std::string>& names) {
    if (names.empty()) {
        return;
    }
    for (size_t i = from; i < to && i < tokens.size(); ++i) {
        if (tokens[i].type == b::lexer::TokenType::IDENTIFIER && names.count(tokens[i].lexeme)) {
            frozen.insert(i);
        }
    }
}

size_t NamespaceResolver::collectStruct(size_t index) {
    size_t i = index + 1;
    if (!isType(i, b::lexer::TokenType::IDENTIFIER)) {
        return skipToSemicolon(index);
    }
    size_t nameIndex = i;
    declare(tokens[i].lexeme, i);
    ++i;

    std::unordered_set<std::string> typeParams;
    if (isType(i, b::lexer::TokenType::LESS)) {
        i = readTypeParams(i, typeParams);
    }
    if (!isType(i, b::lexer::TokenType::LBRACE)) {
        return skipToSemicolon(nameIndex);
    }

    size_t end = skipBalanced(i, b::lexer::TokenType::LBRACE, b::lexer::TokenType::RBRACE);
    freezeFieldNames(i + 1, end > 0 ? end - 1 : 0);
    freezeMatching(nameIndex, end, typeParams);
    if (isType(end, b::lexer::TokenType::SEMICOLON)) {
        ++end;
    }
    return end;
}

size_t NamespaceResolver::collectEnum(size_t index) {
    size_t i = index + 1;
    if (!isType(i, b::lexer::TokenType::IDENTIFIER)) {
        return skipToSemicolon(index);
    }
    declare(tokens[i].lexeme, i);
    ++i;
    if (!isType(i, b::lexer::TokenType::LBRACE)) {
        return skipToSemicolon(index);
    }

    size_t end = skipBalanced(i, b::lexer::TokenType::LBRACE, b::lexer::TokenType::RBRACE);
    ++i;

    while (i + 1 < end && isType(i, b::lexer::TokenType::IDENTIFIER)) {
        declare(tokens[i].lexeme, i);
        ++i;
        if (isType(i, b::lexer::TokenType::EQUAL)) {
            ++i;
            if (isType(i, b::lexer::TokenType::MINUS)) ++i;
            if (isType(i, b::lexer::TokenType::INTEGER)) ++i;
        }
        if (!isType(i, b::lexer::TokenType::COMMA)) {
            break;
        }
        ++i;
    }

    if (isType(end, b::lexer::TokenType::SEMICOLON)) {
        ++end;
    }
    return end;
}

size_t NamespaceResolver::collectTypedef(size_t index) {
    size_t end = skipToSemicolon(index);
    for (size_t i = index; i + 2 < end; ++i) {
        if (tokens[i].type == b::lexer::TokenType::LPAREN &&
            tokens[i + 1].type == b::lexer::TokenType::STAR &&
            tokens[i + 2].type == b::lexer::TokenType::IDENTIFIER) {
            declare(tokens[i + 2].lexeme, i + 2);
            break;
        }
    }
    return end;
}

size_t NamespaceResolver::skipDropDeclaration(size_t index) {
    size_t i = index;
    while (i < tokens.size() && !isType(i, b::lexer::TokenType::LPAREN)) {
        if (isType(i, b::lexer::TokenType::EOF_TOKEN)) {
            return i;
        }
        ++i;
    }
    i = skipBalanced(i, b::lexer::TokenType::LPAREN, b::lexer::TokenType::RPAREN);
    if (isType(i, b::lexer::TokenType::LBRACE)) {
        i = skipBalanced(i, b::lexer::TokenType::LBRACE, b::lexer::TokenType::RBRACE);
    }
    return i > index ? i : index + 1;
}

size_t NamespaceResolver::collectFunctionOrGlobal(size_t index) {
    size_t i = index;
    if (isType(i, b::lexer::TokenType::KW_PUB)) {
        ++i;
    }
    if (isType(i, b::lexer::TokenType::KW_CONST)) {
        ++i;
    }
    if (isType(i, b::lexer::TokenType::KW_OWN)) {
        ++i;
    } else if (isType(i, b::lexer::TokenType::AMPERSAND)) {
        ++i;
        if (isType(i, b::lexer::TokenType::KW_MUT)) {
            ++i;
        }
    }
    if (isType(i, b::lexer::TokenType::LBRACKET)) {
        i = skipBalanced(i, b::lexer::TokenType::LBRACKET, b::lexer::TokenType::RBRACKET);
        if (isType(i, b::lexer::TokenType::QUESTION)) {
            ++i;
        }
    }

    size_t nameIndex = tokens.size();
    size_t terminator = tokens.size();
    int angle = 0;
    for (size_t k = i; k < tokens.size(); ++k) {
        b::lexer::TokenType type = tokens[k].type;
        if (type == b::lexer::TokenType::LESS) {
            ++angle;
        } else if (type == b::lexer::TokenType::GREATER) {
            if (angle > 0) --angle;
        } else if (type == b::lexer::TokenType::GREATER_GREATER) {
            angle = angle > 1 ? angle - 2 : 0;
        } else if (type == b::lexer::TokenType::IDENTIFIER) {
            if (angle == 0) nameIndex = k;
        } else if (type == b::lexer::TokenType::SEMICOLON ||
                   type == b::lexer::TokenType::LBRACE ||
                   type == b::lexer::TokenType::RBRACE ||
                   type == b::lexer::TokenType::EOF_TOKEN) {
            terminator = k;
            break;
        } else if (angle == 0 && (type == b::lexer::TokenType::LPAREN ||
                                  type == b::lexer::TokenType::EQUAL ||
                                  type == b::lexer::TokenType::LBRACKET)) {
            terminator = k;
            break;
        }
    }

    if (nameIndex < tokens.size()) {
        declare(tokens[nameIndex].lexeme, nameIndex);
    }

    std::unordered_set<std::string> typeParams;
    if (nameIndex + 1 < tokens.size() && isType(nameIndex + 1, b::lexer::TokenType::LESS)) {
        readTypeParams(nameIndex + 1, typeParams);
    }

    size_t end;
    if (terminator >= tokens.size()) {
        end = tokens.size();
    } else if (tokens[terminator].type == b::lexer::TokenType::LPAREN) {
        size_t afterParams =
            skipBalanced(terminator, b::lexer::TokenType::LPAREN, b::lexer::TokenType::RPAREN);
        if (isType(afterParams, b::lexer::TokenType::LBRACE)) {
            end = skipBalanced(afterParams, b::lexer::TokenType::LBRACE,
                               b::lexer::TokenType::RBRACE);
        } else {
            end = skipToSemicolon(afterParams);
        }
    } else if (tokens[terminator].type == b::lexer::TokenType::RBRACE) {
        end = terminator;
    } else {
        end = skipToSemicolon(terminator);
    }

    freezeMatching(index, end, typeParams);
    return end > index ? end : index + 1;
}

void NamespaceResolver::collect() {
    nsStack.clear();
    depth = 0;

    size_t i = 0;
    while (i < tokens.size()) {
        b::lexer::TokenType type = tokens[i].type;

        if (type == b::lexer::TokenType::EOF_TOKEN) {
            ++i;
            continue;
        }

        if (!atDeclarationLevel()) {
            if (type == b::lexer::TokenType::LBRACE) {
                ++depth;
            } else if (type == b::lexer::TokenType::RBRACE && depth > 0) {
                --depth;
                closeNamespaces();
            }
            ++i;
            continue;
        }

        if (type == b::lexer::TokenType::KW_NAMESPACE) {
            i = openNamespace(i);
            continue;
        }
        if (type == b::lexer::TokenType::KW_USING) {
            i = skipToSemicolon(i);
            continue;
        }
        if (type == b::lexer::TokenType::RBRACE) {
            if (depth > 0) {
                --depth;
                closeNamespaces();
            }
            ++i;
            continue;
        }

        if (type == b::lexer::TokenType::KW_DROP) {
            i = skipDropDeclaration(i);
            continue;
        }

        size_t declStart = i;
        if (type == b::lexer::TokenType::KW_PUB && declStart + 1 < tokens.size()) {
            type = tokens[declStart + 1].type;
            ++declStart;
        }

        size_t next;
        if (type == b::lexer::TokenType::KW_STRUCT) {
            next = collectStruct(declStart);
        } else if (type == b::lexer::TokenType::KW_ENUM) {
            next = collectEnum(declStart);
        } else if (type == b::lexer::TokenType::KW_TYPEDEF) {
            next = collectTypedef(declStart);
        } else {
            next = collectFunctionOrGlobal(i);
        }
        i = next > i ? next : i + 1;
    }

    if (!nsStack.empty()) {
        throw b::CompilerException("Unterminated namespace '" + nsStack.back().qualified + "'");
    }
}

void NamespaceResolver::freezeInitializerFieldNames() {
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i].type != b::lexer::TokenType::KW_NEW) {
            continue;
        }
        size_t brace = i + 1;
        while (brace < tokens.size() && !isType(brace, b::lexer::TokenType::LBRACE)) {
            if (isType(brace, b::lexer::TokenType::SEMICOLON) ||
                isType(brace, b::lexer::TokenType::LPAREN) ||
                isType(brace, b::lexer::TokenType::EOF_TOKEN)) {
                break;
            }
            ++brace;
        }
        if (!isType(brace, b::lexer::TokenType::LBRACE)) {
            continue;
        }

        size_t end = skipBalanced(brace, b::lexer::TokenType::LBRACE, b::lexer::TokenType::RBRACE);
        int depth = 0;
        for (size_t k = brace; k + 1 < end; ++k) {
            if (tokens[k].type == b::lexer::TokenType::LBRACE) {
                ++depth;
            } else if (tokens[k].type == b::lexer::TokenType::RBRACE) {
                --depth;
            } else if (depth == 1 && tokens[k].type == b::lexer::TokenType::IDENTIFIER &&
                       isType(k + 1, b::lexer::TokenType::COLON)) {
                frozen.insert(k);
            }
        }
    }
}

void NamespaceResolver::checkCollisions() const {
    std::unordered_map<std::string, std::string> seen;
    for (const auto& entry : scopes) {
        for (const auto& name : entry.second.names) {
            std::string qualified = join(entry.first, name);
            std::string flat = mangle(qualified);
            auto it = seen.find(flat);
            if (it != seen.end() && it->second != qualified) {
                throw b::CompilerException("'" + qualified + "' and '" + it->second +
                                           "' both flatten to '" + flat +
                                           "'; rename one of them");
            }
            seen.emplace(flat, qualified);
        }
    }
}

std::string NamespaceResolver::findBaseScope(const std::string& first, size_t index) const {
    for (auto it = nsStack.rbegin(); it != nsStack.rend(); ++it) {
        const Scope* scope = findScope(it->qualified);
        if (scope && scope->children.count(first)) {
            return it->qualified;
        }
    }

    const Scope* global = findScope("");
    if (global && global->children.count(first)) {
        return "";
    }

    for (auto it = usings.rbegin(); it != usings.rend(); ++it) {
        const Scope* scope = findScope(it->qualified);
        if (scope && scope->children.count(first)) {
            return it->qualified;
        }
    }

    throw b::CompilerException("Unknown namespace '" + first + "'" + at(index));
}

std::string NamespaceResolver::resolveFrom(const std::string& base,
                                           const std::vector<std::string>& parts,
                                           size_t index) const {
    std::string current = base;
    for (size_t k = 0; k + 1 < parts.size(); ++k) {
        const Scope* scope = findScope(current);
        if (!scope || !scope->children.count(parts[k])) {
            throw b::CompilerException("Namespace '" + (current.empty() ? "(global)" : current) +
                                       "' has no namespace '" + parts[k] + "'" + at(index));
        }
        current = join(current, parts[k]);
    }

    const std::string& last = parts.back();
    const Scope* scope = findScope(current);
    if (!scope || !scope->names.count(last)) {
        throw b::CompilerException("'" + last + "' is not declared in namespace '" +
                                   (current.empty() ? "(global)" : current) + "'" + at(index));
    }
    return current.empty() ? last : mangle(join(current, last));
}

std::string NamespaceResolver::resolveNamespace(const std::vector<std::string>& parts,
                                                size_t index) const {
    std::string current = findBaseScope(parts[0], index);
    for (const auto& part : parts) {
        const Scope* scope = findScope(current);
        if (!scope || !scope->children.count(part)) {
            throw b::CompilerException("Namespace '" + (current.empty() ? "(global)" : current) +
                                       "' has no namespace '" + part + "'" + at(index));
        }
        current = join(current, part);
    }
    return current;
}

std::string NamespaceResolver::resolveUnqualified(const std::string& name, size_t index) const {
    for (auto it = nsStack.rbegin(); it != nsStack.rend(); ++it) {
        const Scope* scope = findScope(it->qualified);
        if (scope && scope->names.count(name)) {
            return mangle(join(it->qualified, name));
        }
    }

    const Scope* global = findScope("");
    if (global && global->names.count(name)) {
        return "";
    }

    std::string found;
    std::string foundIn;
    for (auto it = usings.rbegin(); it != usings.rend(); ++it) {
        const Scope* scope = findScope(it->qualified);
        if (!scope || !scope->names.count(name)) {
            continue;
        }
        std::string candidate = mangle(join(it->qualified, name));
        if (found.empty()) {
            found = candidate;
            foundIn = it->qualified;
        } else if (found != candidate) {
            throw b::CompilerException("'" + name + "' is ambiguous: it is declared in '" +
                                       foundIn + "' and in '" + it->qualified + "'" + at(index));
        }
    }
    return found;
}

size_t NamespaceResolver::applyUsing(size_t index) {
    size_t i = index + 1;
    if (!isType(i, b::lexer::TokenType::KW_NAMESPACE)) {
        throw b::CompilerException("Expected 'namespace' after 'using'" + at(index));
    }
    ++i;

    std::vector<std::string> parts = readPath(i);
    if (parts.empty()) {
        throw b::CompilerException("Expected a namespace name after 'using namespace'" + at(index));
    }
    if (!isType(i, b::lexer::TokenType::SEMICOLON)) {
        throw b::CompilerException("Expected ';' after 'using namespace'" + at(i));
    }
    ++i;

    usings.push_back({resolveNamespace(parts, index), depth, tokens[index].file});
    return i;
}

size_t NamespaceResolver::emitIdentifier(size_t index, std::vector<b::lexer::Token>& out) {
    b::lexer::Token token = tokens[index];

    auto site = declSites.find(index);
    if (site != declSites.end()) {
        token.lexeme = site->second;
        token.value = site->second;
        out.push_back(token);
        return index + 1;
    }

    if (frozen.count(index)) {
        out.push_back(token);
        return index + 1;
    }

    if (index > 0 && (tokens[index - 1].type == b::lexer::TokenType::DOT ||
                      tokens[index - 1].type == b::lexer::TokenType::ARROW)) {
        out.push_back(token);
        return index + 1;
    }

    if (isType(index + 1, b::lexer::TokenType::COLON_COLON)) {
        size_t next = index;
        std::vector<std::string> parts = readPath(next);
        std::string flat = resolveFrom(findBaseScope(parts[0], index), parts, index);
        token.lexeme = flat;
        token.value = flat;
        out.push_back(token);
        return next;
    }

    std::string flat = resolveUnqualified(token.lexeme, index);
    if (!flat.empty()) {
        token.lexeme = flat;
        token.value = flat;
    }
    out.push_back(token);
    return index + 1;
}

std::vector<b::lexer::Token> NamespaceResolver::rewrite() {
    std::vector<b::lexer::Token> out;
    out.reserve(tokens.size());

    nsStack.clear();
    usings.clear();
    depth = 0;
    std::string currentFile;

    size_t i = 0;
    while (i < tokens.size()) {
        const b::lexer::Token& token = tokens[i];

        if (!token.file.empty() && token.file != currentFile) {
            currentFile = token.file;
            while (!usings.empty() && usings.back().file != currentFile) {
                usings.pop_back();
            }
        }

        if (token.type == b::lexer::TokenType::KW_NAMESPACE && atDeclarationLevel()) {
            i = openNamespace(i);
            continue;
        }

        if (token.type == b::lexer::TokenType::KW_USING) {
            i = applyUsing(i);
            continue;
        }

        if (token.type == b::lexer::TokenType::COLON_COLON) {

            size_t next = i + 1;
            std::vector<std::string> parts = readPath(next);
            if (!parts.empty()) {
                b::lexer::Token renamed = tokens[next - 1];
                std::string flat = resolveFrom("", parts, i);
                renamed.lexeme = flat;
                renamed.value = flat;
                out.push_back(renamed);
                i = next;
                continue;
            }
        }

        if (token.type == b::lexer::TokenType::LBRACE) {
            ++depth;
        } else if (token.type == b::lexer::TokenType::RBRACE) {
            if (!nsStack.empty() && depth == nsStack.back().depth + 1) {
                --depth;
                closeNamespaces();
                while (!usings.empty() && usings.back().depth > depth) {
                    usings.pop_back();
                }
                ++i;
                continue;
            }
            if (depth > 0) {
                --depth;
            }
            while (!usings.empty() && usings.back().depth > depth) {
                usings.pop_back();
            }
        }

        if (token.type == b::lexer::TokenType::IDENTIFIER) {
            i = emitIdentifier(i, out);
            continue;
        }

        out.push_back(token);
        ++i;
    }

    return out;
}

std::vector<b::lexer::Token> NamespaceResolver::resolve(std::vector<b::lexer::Token> input) {
    tokens = std::move(input);
    scopes.clear();
    declared.clear();
    declSites.clear();
    frozen.clear();

    bool usesNamespaces = false;
    for (const auto& token : tokens) {
        if (token.type == b::lexer::TokenType::KW_NAMESPACE ||
            token.type == b::lexer::TokenType::KW_USING ||
            token.type == b::lexer::TokenType::COLON_COLON) {
            usesNamespaces = true;
            break;
        }
    }
    if (!usesNamespaces) {
        return std::move(tokens);
    }

    freezeInitializerFieldNames();
    collect();
    checkCollisions();
    return rewrite();
}

}

void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " <source.b> [--debug]" << std::endl;
}

void printVersion() {
    std::cout << "B 1.0.0" << std::endl;
}

class ASTPrinter : public b::ast::ASTVisitor {
public:
    void visit(b::ast::Literal* node) override {
        std::cout << "Literal(" << node->value << ")";
    }

    void visit(b::ast::SizeofExpr* node) override {
        std::cout << "Sizeof(" << b::ast::typeToString(node->targetType) << ")";
    }

    void visit(b::ast::NewExpr* node) override {
        std::cout << "New(" << b::ast::typeToString(node->type) << ")";
    }

    void visit(b::ast::NewSliceExpr* node) override {
        std::cout << "NewSlice(" << b::ast::typeToString(node->type) << ")";
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

    void visit(b::ast::IfSomeStmt* node) override {
        std::cout << "IfSome(" << node->binding << ")";
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
        std::cout << "Updating B..." << std::endl;
#if defined(_WIN32)
        std::string command =
            "powershell -NoProfile -ExecutionPolicy Bypass -Command "
            "\"if (Test-Path \\\"$env:USERPROFILE\\.b\\repo\\get.ps1\\\") { "
            "powershell -ExecutionPolicy Bypass -File "
            "\\\"$env:USERPROFILE\\.b\\repo\\get.ps1\\\" -Action update "
            "} else { irm https://raw.githubusercontent.com/ital87/B/main/get.ps1 | iex }\"";
#else
        std::string command =
            "bash -c 'if [ -f \"$HOME/.b/repo/get.sh\" ]; then "
            "bash \"$HOME/.b/repo/get.sh\" update; "
            "else "
            "curl -fsSL https://raw.githubusercontent.com/ital87/B/main/get.sh | bash; "
            "fi'";
#endif
        int result = std::system(command.c_str());
        return result == 0 ? 0 : 1;
    }

    std::string filepath = firstArg;
    bool debugMode = (argc > 2 && std::string(argv[2]) == "--debug");

    try {
        b::diag::Reporter reporter;

        std::cout << "[1/6] Loading modules..." << std::endl;
        ModuleLoader loader;
        auto tokens = loader.load(filepath);
        for (const auto& entry : loader.sources()) {
            reporter.addSource(entry.first, entry.second);
        }

        std::cout << "[2/6] Lexing..." << std::endl;
        std::cout << "      Modules: " << loader.modules().size() << std::endl;
        if (debugMode) {
            for (const auto& moduleName : loader.modules()) {
                std::cout << "        " << moduleName << std::endl;
            }
        }
        if (debugMode) {
            for (const auto& cycle : loader.cycles()) {
                std::cerr << "      Import cycle: " << cycle << std::endl;
            }
        }
        std::cout << "      Tokens: " << tokens.size() - 1 << std::endl;

        std::cout << "[3/6] Resolving namespaces..." << std::endl;
        b::modules::NamespaceResolver resolver;
        tokens = resolver.resolve(std::move(tokens));
        std::cout << "      Namespaces: " << resolver.namespaces().size() << std::endl;
        if (debugMode) {
            for (const auto& name : resolver.namespaces()) {
                std::cout << "        " << name << std::endl;
            }
        }

        std::cout << "[4/6] Parsing..." << std::endl;
        b::parser::Parser parser(tokens);
        auto program = parser.parse();
        std::cout << "      Functions: " << program->functions.size() << std::endl;

        if (debugMode) {
            std::cout << "      AST:" << std::endl;
            ASTPrinter printer;
            program->accept(&printer);
            std::cout << std::endl;
        }

        std::cout << "[5/6] Checking..." << std::endl;
        b::sema::Analyzer analyzer(reporter);
        analyzer.run(program.get());
        if (!reporter.empty()) {
            reporter.print(std::cerr);
        }
        if (reporter.failed()) {
            return 1;
        }

        std::cout << "[6/6] Code generation..." << std::endl;

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
