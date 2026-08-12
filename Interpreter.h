#pragma once

#include "Stmt.h"
#include "Vm.h"

namespace sek {

class Interpreter {
public:
    Interpreter();
    void execute(const Program& program);
};

}  // namespace sek
