#pragma once

#include <string>
#include <memory>
#include <stdexcept>

namespace arc {

class CompilerException : public std::runtime_error {
public:
    explicit CompilerException(const std::string& message)
        : std::runtime_error(message) {
    }
};

class ParseException : public CompilerException {
public:
    explicit ParseException(const std::string& message)
        : CompilerException("Parse Error: " + message) {
    }
};

class LexerException : public CompilerException {
public:
    explicit LexerException(const std::string& message)
        : CompilerException("Lexer Error: " + message) {
    }
};

class CodegenException : public CompilerException {
public:
    explicit CodegenException(const std::string& message)
        : CompilerException("Codegen Error: " + message) {
    }
};

} // namespace arc

