#include "Lexer.h"
#include <cctype>
#include <stdexcept>

namespace arc::lexer {

const std::unordered_map<std::string, TokenType> Lexer::keywords = {
    {"int", TokenType::KW_INT},
    {"float", TokenType::KW_FLOAT},
    {"double", TokenType::KW_DOUBLE},
    {"bool", TokenType::KW_BOOL},
    {"char", TokenType::KW_CHAR},
    {"void", TokenType::KW_VOID},
    {"return", TokenType::KW_RETURN},
    {"if", TokenType::KW_IF},
    {"else", TokenType::KW_ELSE},
    {"for", TokenType::KW_FOR},
    {"while", TokenType::KW_WHILE},
    {"struct", TokenType::KW_STRUCT},
    {"enum", TokenType::KW_ENUM},
    {"fn", TokenType::KW_FUNCTION},
    {"true", TokenType::KW_TRUE},
    {"false", TokenType::KW_FALSE},
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
    if (!isAtEnd()) advance(); // Skip newline
}

void Lexer::skipBlockComment() {
    advance(); // Skip /
    advance(); // Skip *

    while (!isAtEnd()) {
        if (peek() == '*' && peekNext() == '/') {
            advance(); // Skip *
            advance(); // Skip /
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

    advance(); // Closing quote

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

} // namespace arc::lexer

