#include "Parser.h"
#include <sstream>

namespace arc::parser {

Parser::Parser(const std::vector<arc::lexer::Token>& tokens)
    : tokens(tokens), current(0) {}

std::unique_ptr<arc::ast::Program> Parser::parse() {
    std::vector<std::unique_ptr<arc::ast::StructDecl>> structs;
    std::vector<std::unique_ptr<arc::ast::FunctionDecl>> functions;

    while (!isAtEnd()) {
        if (check(arc::lexer::TokenType::EOF_TOKEN)) break;
        if (check(arc::lexer::TokenType::KW_STRUCT)) {
            structs.push_back(parseStructDecl());
        } else {
            functions.push_back(parseFunctionDecl());
        }
    }

    return std::make_unique<arc::ast::Program>(std::move(structs), std::move(functions));
}

arc::lexer::Token Parser::peek() const {
    return tokens[current];
}

arc::lexer::Token Parser::previous() const {
    return tokens[current - 1];
}

arc::lexer::Token Parser::advance() {
    if (!isAtEnd()) current++;
    return previous();
}

bool Parser::check(arc::lexer::TokenType type) const {
    if (isAtEnd()) return false;
    return peek().type == type;
}

bool Parser::match(arc::lexer::TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::match(const std::vector<arc::lexer::TokenType>& types) {
    for (auto type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

arc::lexer::Token Parser::consume(arc::lexer::TokenType type, const std::string& message) {
    if (check(type)) return advance();
    throw ParseException(message);
}

bool Parser::isAtEnd() const {
    return peek().type == arc::lexer::TokenType::EOF_TOKEN;
}

arc::ast::Type Parser::parseType() {
    arc::ast::Type type;
    type.pointerLevel = 0;

    if (match(arc::lexer::TokenType::KW_INT)) {
        type.base = arc::ast::PrimitiveType::INT;
    } else if (match(arc::lexer::TokenType::KW_FLOAT)) {
        type.base = arc::ast::PrimitiveType::FLOAT;
    } else if (match(arc::lexer::TokenType::KW_DOUBLE)) {
        type.base = arc::ast::PrimitiveType::DOUBLE;
    } else if (match(arc::lexer::TokenType::KW_BOOL)) {
        type.base = arc::ast::PrimitiveType::BOOL;
    } else if (match(arc::lexer::TokenType::KW_CHAR)) {
        type.base = arc::ast::PrimitiveType::CHAR;
    } else if (match(arc::lexer::TokenType::KW_VOID)) {
        type.base = arc::ast::PrimitiveType::VOID;
    } else if (check(arc::lexer::TokenType::IDENTIFIER)) {
        type.structName = advance().lexeme;
        type.base = arc::ast::PrimitiveType::INT;
    } else {
        throw ParseException("Expected type, got " + peek().lexeme);
    }

    while (match(arc::lexer::TokenType::STAR)) {
        type.pointerLevel++;
    }

    return type;
}

std::vector<arc::ast::Parameter> Parser::parseParameterList() {
    std::vector<arc::ast::Parameter> parameters;

    if (!check(arc::lexer::TokenType::RPAREN)) {
        do {
            arc::ast::Type type = parseType();
            std::string name = consume(arc::lexer::TokenType::IDENTIFIER, "Expected parameter name").lexeme;
            parameters.emplace_back(type, name);
        } while (match(arc::lexer::TokenType::COMMA));
    }

    return parameters;
}

std::unique_ptr<arc::ast::StructDecl> Parser::parseStructDecl() {
    consume(arc::lexer::TokenType::KW_STRUCT, "Expected 'struct'");
    std::string structName = consume(arc::lexer::TokenType::IDENTIFIER, "Expected struct name").lexeme;
    consume(arc::lexer::TokenType::LBRACE, "Expected '{' after struct name");

    std::vector<arc::ast::StructField> fields;
    while (!check(arc::lexer::TokenType::RBRACE) && !isAtEnd()) {
        arc::ast::Type fieldType = parseType();
        std::string fieldName = consume(arc::lexer::TokenType::IDENTIFIER, "Expected field name").lexeme;
        consume(arc::lexer::TokenType::SEMICOLON, "Expected ';' after field");
        fields.emplace_back(fieldType, fieldName);
    }

    consume(arc::lexer::TokenType::RBRACE, "Expected '}' after struct body");
    consume(arc::lexer::TokenType::SEMICOLON, "Expected ';' after struct declaration");

    return std::make_unique<arc::ast::StructDecl>(structName, std::move(fields));
}

std::unique_ptr<arc::ast::FunctionDecl> Parser::parseFunctionDecl() {
    arc::ast::Type returnType = parseType();
    std::string functionName = consume(arc::lexer::TokenType::IDENTIFIER, "Expected function name").lexeme;

    consume(arc::lexer::TokenType::LPAREN, "Expected '(' after function name");
    auto parameters = parseParameterList();
    consume(arc::lexer::TokenType::RPAREN, "Expected ')' after parameters");

    consume(arc::lexer::TokenType::LBRACE, "Expected '{' before function body");
    auto body = std::unique_ptr<arc::ast::Block>(
        dynamic_cast<arc::ast::Block*>(parseBlock().release())
    );
    consume(arc::lexer::TokenType::RBRACE, "Expected '}' after function body");

    return std::make_unique<arc::ast::FunctionDecl>(
        returnType, functionName, std::move(parameters), std::move(body)
    );
}

std::unique_ptr<arc::ast::Statement> Parser::parseStatement() {
    if (match(arc::lexer::TokenType::LBRACE)) {
        auto block = parseBlock();
        consume(arc::lexer::TokenType::RBRACE, "Expected '}' after block");
        return block;
    }
    if (check(arc::lexer::TokenType::KW_INT) ||
        check(arc::lexer::TokenType::KW_FLOAT) ||
        check(arc::lexer::TokenType::KW_DOUBLE) ||
        check(arc::lexer::TokenType::KW_BOOL) ||
        check(arc::lexer::TokenType::KW_CHAR) ||
        check(arc::lexer::TokenType::KW_VOID)) {
        return parseVariableDecl();
    }
    if (check(arc::lexer::TokenType::IDENTIFIER)) {
        size_t lookAhead = current + 1;
        while (lookAhead < tokens.size() &&
               (tokens[lookAhead].type == arc::lexer::TokenType::STAR ||
                tokens[lookAhead].type == arc::lexer::TokenType::AMPERSAND)) {
            lookAhead++;
        }
        if (lookAhead < tokens.size() &&
            tokens[lookAhead].type == arc::lexer::TokenType::IDENTIFIER) {
            return parseVariableDecl();
        }
    }
    if (match(arc::lexer::TokenType::KW_IF)) {
        return parseIfStmt();
    }
    if (match(arc::lexer::TokenType::KW_FOR)) {
        return parseForStmt();
    }
    if (match(arc::lexer::TokenType::KW_WHILE)) {
        return parseWhileStmt();
    }
    if (match(arc::lexer::TokenType::KW_RETURN)) {
        return parseReturnStmt();
    }

    return parseExpressionStmt();
}

std::unique_ptr<arc::ast::Statement> Parser::parseBlock() {
    std::vector<std::unique_ptr<arc::ast::Statement>> statements;

    while (!check(arc::lexer::TokenType::RBRACE) && !isAtEnd()) {
        statements.push_back(parseStatement());
    }

    return std::make_unique<arc::ast::Block>(std::move(statements));
}

std::unique_ptr<arc::ast::Statement> Parser::parseVariableDecl() {
    arc::ast::Type type = parseType();
    std::string name = consume(arc::lexer::TokenType::IDENTIFIER, "Expected variable name").lexeme;

    std::unique_ptr<arc::ast::Expression> initializer = nullptr;
    if (match(arc::lexer::TokenType::EQUAL)) {
        initializer = parseExpression();
    }

    consume(arc::lexer::TokenType::SEMICOLON, "Expected ';' after variable declaration");
    return std::make_unique<arc::ast::VariableDecl>(type, name, std::move(initializer));
}

std::unique_ptr<arc::ast::Statement> Parser::parseIfStmt() {
    consume(arc::lexer::TokenType::LPAREN, "Expected '(' after 'if'");
    auto condition = parseExpression();
    consume(arc::lexer::TokenType::RPAREN, "Expected ')' after if condition");

    auto thenBranch = parseStatement();

    std::unique_ptr<arc::ast::Statement> elseBranch = nullptr;
    if (match(arc::lexer::TokenType::KW_ELSE)) {
        elseBranch = parseStatement();
    }

    return std::make_unique<arc::ast::IfStmt>(
        std::move(condition), std::move(thenBranch), std::move(elseBranch)
    );
}

std::unique_ptr<arc::ast::Statement> Parser::parseForStmt() {
    consume(arc::lexer::TokenType::LPAREN, "Expected '(' after 'for'");

    std::unique_ptr<arc::ast::Statement> init = nullptr;
    if (match(arc::lexer::TokenType::SEMICOLON)) {
        init = nullptr;
    } else if (check(arc::lexer::TokenType::KW_INT) ||
               check(arc::lexer::TokenType::KW_FLOAT) ||
               check(arc::lexer::TokenType::KW_DOUBLE) ||
               check(arc::lexer::TokenType::KW_BOOL)) {
        init = parseVariableDecl();
    } else {
        init = parseExpressionStmt();
    }

    std::unique_ptr<arc::ast::Expression> condition = nullptr;
    if (!check(arc::lexer::TokenType::SEMICOLON)) {
        condition = parseExpression();
    }
    consume(arc::lexer::TokenType::SEMICOLON, "Expected ';' after for condition");

    std::unique_ptr<arc::ast::Expression> increment = nullptr;
    if (!check(arc::lexer::TokenType::RPAREN)) {
        increment = parseExpression();
    }
    consume(arc::lexer::TokenType::RPAREN, "Expected ')' after for clauses");

    auto body = parseStatement();

    return std::make_unique<arc::ast::ForStmt>(
        std::move(init), std::move(condition), std::move(increment), std::move(body)
    );
}

std::unique_ptr<arc::ast::Statement> Parser::parseWhileStmt() {
    consume(arc::lexer::TokenType::LPAREN, "Expected '(' after 'while'");
    auto condition = parseExpression();
    consume(arc::lexer::TokenType::RPAREN, "Expected ')' after while condition");

    auto body = parseStatement();

    return std::make_unique<arc::ast::WhileStmt>(std::move(condition), std::move(body));
}

std::unique_ptr<arc::ast::Statement> Parser::parseReturnStmt() {
    std::unique_ptr<arc::ast::Expression> value = nullptr;

    if (!check(arc::lexer::TokenType::SEMICOLON)) {
        value = parseExpression();
    }

    consume(arc::lexer::TokenType::SEMICOLON, "Expected ';' after return statement");
    return std::make_unique<arc::ast::ReturnStmt>(std::move(value));
}

std::unique_ptr<arc::ast::Statement> Parser::parseExpressionStmt() {
    auto expr = parseExpression();
    consume(arc::lexer::TokenType::SEMICOLON, "Expected ';' after expression");
    return std::make_unique<arc::ast::ExpressionStmt>(std::move(expr));
}

std::unique_ptr<arc::ast::Expression> Parser::parseExpression() {
    return parseAssignment();
}

std::unique_ptr<arc::ast::Expression> Parser::parseAssignment() {
    auto expr = parseLogicalOr();

    if (match(arc::lexer::TokenType::EQUAL)) {
        auto right = parseAssignment();
        return std::make_unique<arc::ast::BinaryOp>(
            arc::ast::BinaryOp::Operator::ASSIGN,
            std::move(expr),
            std::move(right)
        );
    }

    return expr;
}

std::unique_ptr<arc::ast::Expression> Parser::parseLogicalOr() {
    auto expr = parseLogicalAnd();

    while (match(arc::lexer::TokenType::PIPE_PIPE)) {
        auto right = parseLogicalAnd();
        expr = std::make_unique<arc::ast::BinaryOp>(
            arc::ast::BinaryOp::Operator::LOGICAL_OR,
            std::move(expr),
            std::move(right)
        );
    }

    return expr;
}

std::unique_ptr<arc::ast::Expression> Parser::parseLogicalAnd() {
    auto expr = parseBitwiseOr();

    while (match(arc::lexer::TokenType::AND_AND)) {
        auto right = parseBitwiseOr();
        expr = std::make_unique<arc::ast::BinaryOp>(
            arc::ast::BinaryOp::Operator::LOGICAL_AND,
            std::move(expr),
            std::move(right)
        );
    }

    return expr;
}

std::unique_ptr<arc::ast::Expression> Parser::parseBitwiseOr() {
    auto expr = parseBitwiseXor();

    while (match(arc::lexer::TokenType::PIPE)) {
        auto right = parseBitwiseXor();
        expr = std::make_unique<arc::ast::BinaryOp>(
            arc::ast::BinaryOp::Operator::BITWISE_OR,
            std::move(expr),
            std::move(right)
        );
    }

    return expr;
}

std::unique_ptr<arc::ast::Expression> Parser::parseBitwiseXor() {
    auto expr = parseBitwiseAnd();

    while (match(arc::lexer::TokenType::CARET)) {
        auto right = parseBitwiseAnd();
        expr = std::make_unique<arc::ast::BinaryOp>(
            arc::ast::BinaryOp::Operator::BITWISE_XOR,
            std::move(expr),
            std::move(right)
        );
    }

    return expr;
}

std::unique_ptr<arc::ast::Expression> Parser::parseBitwiseAnd() {
    auto expr = parseEquality();

    while (match(arc::lexer::TokenType::AMPERSAND)) {
        auto right = parseEquality();
        expr = std::make_unique<arc::ast::BinaryOp>(
            arc::ast::BinaryOp::Operator::BITWISE_AND,
            std::move(expr),
            std::move(right)
        );
    }

    return expr;
}

std::unique_ptr<arc::ast::Expression> Parser::parseEquality() {
    auto expr = parseComparison();

    while (true) {
        if (match(arc::lexer::TokenType::EQUAL_EQUAL)) {
            auto right = parseComparison();
            expr = std::make_unique<arc::ast::BinaryOp>(
                arc::ast::BinaryOp::Operator::EQUAL,
                std::move(expr),
                std::move(right)
            );
        } else if (match(arc::lexer::TokenType::NOT_EQUAL)) {
            auto right = parseComparison();
            expr = std::make_unique<arc::ast::BinaryOp>(
                arc::ast::BinaryOp::Operator::NOT_EQUAL,
                std::move(expr),
                std::move(right)
            );
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<arc::ast::Expression> Parser::parseComparison() {
    auto expr = parseAdditive();

    while (true) {
        if (match(arc::lexer::TokenType::LESS)) {
            auto right = parseAdditive();
            expr = std::make_unique<arc::ast::BinaryOp>(
                arc::ast::BinaryOp::Operator::LESS,
                std::move(expr),
                std::move(right)
            );
        } else if (match(arc::lexer::TokenType::LESS_EQUAL)) {
            auto right = parseAdditive();
            expr = std::make_unique<arc::ast::BinaryOp>(
                arc::ast::BinaryOp::Operator::LESS_EQUAL,
                std::move(expr),
                std::move(right)
            );
        } else if (match(arc::lexer::TokenType::GREATER)) {
            auto right = parseAdditive();
            expr = std::make_unique<arc::ast::BinaryOp>(
                arc::ast::BinaryOp::Operator::GREATER,
                std::move(expr),
                std::move(right)
            );
        } else if (match(arc::lexer::TokenType::GREATER_EQUAL)) {
            auto right = parseAdditive();
            expr = std::make_unique<arc::ast::BinaryOp>(
                arc::ast::BinaryOp::Operator::GREATER_EQUAL,
                std::move(expr),
                std::move(right)
            );
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<arc::ast::Expression> Parser::parseAdditive() {
    auto expr = parseMultiplicative();

    while (true) {
        if (match(arc::lexer::TokenType::PLUS)) {
            auto right = parseMultiplicative();
            expr = std::make_unique<arc::ast::BinaryOp>(
                arc::ast::BinaryOp::Operator::PLUS,
                std::move(expr),
                std::move(right)
            );
        } else if (match(arc::lexer::TokenType::MINUS)) {
            auto right = parseMultiplicative();
            expr = std::make_unique<arc::ast::BinaryOp>(
                arc::ast::BinaryOp::Operator::MINUS,
                std::move(expr),
                std::move(right)
            );
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<arc::ast::Expression> Parser::parseMultiplicative() {
    auto expr = parseUnary();

    while (true) {
        if (match(arc::lexer::TokenType::STAR)) {
            auto right = parseUnary();
            expr = std::make_unique<arc::ast::BinaryOp>(
                arc::ast::BinaryOp::Operator::MULTIPLY,
                std::move(expr),
                std::move(right)
            );
        } else if (match(arc::lexer::TokenType::SLASH)) {
            auto right = parseUnary();
            expr = std::make_unique<arc::ast::BinaryOp>(
                arc::ast::BinaryOp::Operator::DIVIDE,
                std::move(expr),
                std::move(right)
            );
        } else if (match(arc::lexer::TokenType::PERCENT)) {
            auto right = parseUnary();
            expr = std::make_unique<arc::ast::BinaryOp>(
                arc::ast::BinaryOp::Operator::MODULO,
                std::move(expr),
                std::move(right)
            );
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<arc::ast::Expression> Parser::parseUnary() {
    if (match(arc::lexer::TokenType::MINUS)) {
        auto operand = parseUnary();
        return std::make_unique<arc::ast::UnaryOp>(
            arc::ast::UnaryOp::Operator::NEGATE,
            std::move(operand)
        );
    }

    if (match(arc::lexer::TokenType::BANG)) {
        auto operand = parseUnary();
        return std::make_unique<arc::ast::UnaryOp>(
            arc::ast::UnaryOp::Operator::NOT,
            std::move(operand)
        );
    }

    if (match(arc::lexer::TokenType::TILDE)) {
        auto operand = parseUnary();
        return std::make_unique<arc::ast::UnaryOp>(
            arc::ast::UnaryOp::Operator::BITWISE_NOT,
            std::move(operand)
        );
    }

    if (match(arc::lexer::TokenType::STAR)) {
        auto operand = parseUnary();
        return std::make_unique<arc::ast::UnaryOp>(
            arc::ast::UnaryOp::Operator::DEREF,
            std::move(operand)
        );
    }

    if (match(arc::lexer::TokenType::AMPERSAND)) {
        auto operand = parseUnary();
        return std::make_unique<arc::ast::UnaryOp>(
            arc::ast::UnaryOp::Operator::ADDRESS_OF,
            std::move(operand)
        );
    }

    return parsePostfix();
}

std::unique_ptr<arc::ast::Expression> Parser::parsePostfix() {
    auto expr = parsePrimary();

    while (true) {
        if (match(arc::lexer::TokenType::LPAREN)) {
            std::vector<std::unique_ptr<arc::ast::Expression>> arguments;

            if (!check(arc::lexer::TokenType::RPAREN)) {
                do {
                    arguments.push_back(parseExpression());
                } while (match(arc::lexer::TokenType::COMMA));
            }

            consume(arc::lexer::TokenType::RPAREN, "Expected ')' after function arguments");

            if (auto* identifier = dynamic_cast<arc::ast::Identifier*>(expr.get())) {
                std::string funcName = identifier->name;
                expr = std::make_unique<arc::ast::FunctionCall>(funcName, std::move(arguments));
            } else {
                throw ParseException("Invalid function call");
            }
        } else if (match(arc::lexer::TokenType::DOT)) {
            std::string memberName = consume(arc::lexer::TokenType::IDENTIFIER, "Expected member name after '.'").lexeme;
            expr = std::make_unique<arc::ast::MemberAccess>(std::move(expr), memberName);
        } else if (match(arc::lexer::TokenType::LBRACKET)) {
            auto indexExpr = parseExpression();
            consume(arc::lexer::TokenType::RBRACKET, "Expected ']' after array index");
            expr = std::make_unique<arc::ast::ArrayAccess>(std::move(expr), std::move(indexExpr));
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<arc::ast::Expression> Parser::parsePrimary() {
    if (match(arc::lexer::TokenType::KW_TRUE)) {
        return std::make_unique<arc::ast::Literal>(arc::ast::Literal::Kind::BOOLEAN, "true");
    }

    if (match(arc::lexer::TokenType::KW_FALSE)) {
        return std::make_unique<arc::ast::Literal>(arc::ast::Literal::Kind::BOOLEAN, "false");
    }

    if (match(arc::lexer::TokenType::INTEGER)) {
        return std::make_unique<arc::ast::Literal>(
            arc::ast::Literal::Kind::INTEGER, previous().value
        );
    }

    if (match(arc::lexer::TokenType::FLOAT)) {
        return std::make_unique<arc::ast::Literal>(
            arc::ast::Literal::Kind::FLOAT, previous().value
        );
    }

    if (match(arc::lexer::TokenType::STRING)) {
        return std::make_unique<arc::ast::Literal>(
            arc::ast::Literal::Kind::STRING, previous().value
        );
    }

    if (match(arc::lexer::TokenType::IDENTIFIER)) {
        return std::make_unique<arc::ast::Identifier>(previous().lexeme);
    }

    if (match(arc::lexer::TokenType::LPAREN)) {
        auto expr = parseExpression();
        consume(arc::lexer::TokenType::RPAREN, "Expected ')' after expression");
        return expr;
    }

    throw ParseException("Unexpected token: " + peek().lexeme);
}

} // namespace arc::parser

