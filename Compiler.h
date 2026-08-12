#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Bytecode.h"
#include "Stmt.h"

namespace sek {

class Compiler {
public:
    static Module compile(const Program& program);

private:
    void collectNames(const Program& program);
    void collectExprNames(const Expr& expression);
    uint32_t addString(const std::string& text);
    uint32_t addNumber(double number);
    uint32_t resolveSlot(const std::string& name);
    uint32_t emit(uint32_t word);
    void patch(uint32_t at, uint32_t value);
    uint32_t here() const { return static_cast<uint32_t>(code_.size()); }

    uint32_t compileFunction(const Program& body, const std::string& name, bool topLevel);
    void compileStatements(const Program& statements, bool topLevel);
    void compileStatement(const Stmt& statement, bool topLevel);
    void compileExpr(const Expr& expression);

    Module module_;
    std::vector<uint32_t> code_;
};

}  // namespace sek
