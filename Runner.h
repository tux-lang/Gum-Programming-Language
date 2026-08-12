#pragma once

#include <string>
#include <vector>

#include "Stmt.h"

namespace sek {

class Runner {
public:
    int runFile(const std::string& filePath) const;

private:
    Program loadProgram(const std::string& filePath, std::vector<std::string>& includeStack) const;
    Program expandProgram(Program program, const std::string& filePath,
                          std::vector<std::string>& includeStack) const;
    Program parseFile(const std::string& filePath) const;
};

}  // namespace sek
