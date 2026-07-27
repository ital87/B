#pragma once

#include "lexer/Token.h"
#include "ast/ASTNode.h"
#include <vector>
#include <memory>
#include <stdexcept>

namespace arc::parser {

class ParseException : public std::runtime_error {
public:
    explicit ParseException(const std::string& message)
        : std::runtime_error("Parse Error: " + message) {}
};

class Parser {
public:
    explicit Parser(const std::vector<arc::lexer::Token>& tokens);

    std::unique_ptr<arc::ast::Program> parse();

private:
    std::vector<arc::lexer::Token> tokens;
    size_t current;

    arc::lexer::Token peek() const;
    arc::lexer::Token previous() const;
    arc::lexer::Token advance();
    bool check(arc::lexer::TokenType type) const;
    bool match(arc::lexer::TokenType type);
    bool match(const std::vector<arc::lexer::TokenType>& types);
    arc::lexer::Token consume(arc::lexer::TokenType type, const std::string& message);
    bool isAtEnd() const;

    std::unique_ptr<arc::ast::StructDecl> parseStructDecl();
    std::unique_ptr<arc::ast::FunctionDecl> parseFunctionDecl();
    std::unique_ptr<arc::ast::Statement> parseStatement();
    std::unique_ptr<arc::ast::Statement> parseBlock();
    std::unique_ptr<arc::ast::Statement> parseVariableDecl();
    std::unique_ptr<arc::ast::Statement> parseIfStmt();
    std::unique_ptr<arc::ast::Statement> parseForStmt();
    std::unique_ptr<arc::ast::Statement> parseWhileStmt();
    std::unique_ptr<arc::ast::Statement> parseReturnStmt();
    std::unique_ptr<arc::ast::Statement> parseExpressionStmt();

    std::unique_ptr<arc::ast::Expression> parseExpression();
    std::unique_ptr<arc::ast::Expression> parseAssignment();
    std::unique_ptr<arc::ast::Expression> parseLogicalOr();
    std::unique_ptr<arc::ast::Expression> parseLogicalAnd();
    std::unique_ptr<arc::ast::Expression> parseBitwiseOr();
    std::unique_ptr<arc::ast::Expression> parseBitwiseXor();
    std::unique_ptr<arc::ast::Expression> parseBitwiseAnd();
    std::unique_ptr<arc::ast::Expression> parseEquality();
    std::unique_ptr<arc::ast::Expression> parseComparison();
    std::unique_ptr<arc::ast::Expression> parseAdditive();
    std::unique_ptr<arc::ast::Expression> parseMultiplicative();
    std::unique_ptr<arc::ast::Expression> parseUnary();
    std::unique_ptr<arc::ast::Expression> parsePostfix();
    std::unique_ptr<arc::ast::Expression> parsePrimary();

    std::vector<arc::ast::Parameter> parseParameterList();
    arc::ast::Type parseType();
};

} // namespace arc::parser

