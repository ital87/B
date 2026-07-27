#pragma once

#include "Token.h"
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

namespace arc::lexer {

class Lexer {
public:
    explicit Lexer(const std::string& source);

    std::vector<Token> tokenize();

private:
    std::string source;
    size_t current;
    int line;
    int column;

    // Helper methods
    char peek() const;
    char peekNext() const;
    char advance();
    void skipWhitespace();
    void skipLineComment();
    void skipBlockComment();
    bool isAtEnd() const;

    // Token recognition
    Token readString(char quote);
    Token readNumber();
    Token readIdentifierOrKeyword();
    Token makeToken(TokenType type, const std::string& lexeme, const std::string& value = "");

    // Keyword map
    static const std::unordered_map<std::string, TokenType> keywords;
};

} // namespace arc::lexer

