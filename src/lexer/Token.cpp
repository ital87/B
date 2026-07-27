#include "Token.h"

namespace arc::lexer {

Token::Token()
    : type(TokenType::UNKNOWN), lexeme(""), value(""), line(0), column(0) {
}

Token::Token(TokenType type, const std::string& lexeme, const std::string& value, int line, int column)
    : type(type), lexeme(lexeme), value(value), line(line), column(column) {
}

std::string Token::typeToString() const {
    switch (type) {
        // Literals
        case TokenType::INTEGER:        return "INTEGER";
        case TokenType::FLOAT:          return "FLOAT";
        case TokenType::STRING:         return "STRING";
        case TokenType::IDENTIFIER:     return "IDENTIFIER";

        // Keywords
        case TokenType::KW_INT:         return "KW_INT";
        case TokenType::KW_FLOAT:       return "KW_FLOAT";
        case TokenType::KW_DOUBLE:      return "KW_DOUBLE";
        case TokenType::KW_BOOL:        return "KW_BOOL";
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

        // Operators
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

        // Delimiters
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

        // Special
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

} // namespace arc::lexer

