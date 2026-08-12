#pragma once

#include <string>

#include "TokenType.h"

namespace sek {

struct Token {
    TokenType type {};
    std::string lexeme;
    int line = 1;
    std::string filename;
};

}  // namespace sek
