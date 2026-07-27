#pragma once

#include <string>
#include <ostream>

namespace arc::lexer {

enum class TokenType {
    // Literals
    INTEGER,
    FLOAT,
    STRING,
    IDENTIFIER,

    // Keywords
    KW_INT,
    KW_FLOAT,
    KW_DOUBLE,
    KW_BOOL,
    KW_CHAR,
    KW_VOID,
    KW_RETURN,
    KW_IF,
    KW_ELSE,
    KW_FOR,
    KW_WHILE,
    KW_STRUCT,
    KW_ENUM,
    KW_FUNCTION,
    KW_TRUE,
    KW_FALSE,

    // Operators
    PLUS,           // +
    MINUS,          // -
    STAR,           // *
    SLASH,          // /
    PERCENT,        // %
    EQUAL,          // =
    EQUAL_EQUAL,    // ==
    NOT_EQUAL,      // !=
    LESS,           // <
    LESS_EQUAL,     // <=
    GREATER,        // >
    GREATER_EQUAL,  // >=
    AMPERSAND,      // &
    PIPE,           // |
    CARET,          // ^
    TILDE,          // ~
    AND_AND,        // &&
    PIPE_PIPE,      // ||
    BANG,           // !

    // Delimiters
    LPAREN,         // (
    RPAREN,         // )
    LBRACE,         // {
    RBRACE,         // }
    LBRACKET,       // [
    RBRACKET,       // ]
    SEMICOLON,      // ;
    COLON,          // :
    COMMA,          // ,
    DOT,            // .
    ARROW,          // ->

    // Special
    EOF_TOKEN,
    UNKNOWN,
};

struct Token {
    TokenType type;
    std::string lexeme;
    std::string value;  // For literals (strings, numbers)
    int line;
    int column;

    Token();
    Token(TokenType type, const std::string& lexeme, const std::string& value, int line, int column);

    std::string typeToString() const;
};

std::ostream& operator<<(std::ostream& os, const Token& token);

} // namespace arc::lexer

