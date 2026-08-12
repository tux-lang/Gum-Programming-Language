#include "Interpreter.h"

#include <utility>

#include "Compiler.h"

namespace sek {

Interpreter::Interpreter() {}

void Interpreter::execute(const Program& program) {
    Module module = Compiler::compile(program);
    Vm vm(std::move(module));
    vm.run();
}

}  // namespace sek
