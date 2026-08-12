#pragma once

#include <string>
#include <vector>

#include "Token.h"

namespace sek {

class Lexer {
public:
    explicit Lexer(std::string source, std::string filename);

    std::vector<Token> scanTokens() const;

private:
    static bool isAtEnd(std::size_t current, const std::string& source);
    static bool isDigit(char ch);
    static bool isAlpha(char ch);
    static bool isAlphaNumeric(char ch);

    std::string source_;
    std::string filename_;
};

}  // namespace sek
