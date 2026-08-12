#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Vm.h"

namespace sek {

struct JitData {
    double* valueStack = nullptr;
    double* valueStackEnd = nullptr;
    double* loopStack = nullptr;
    double* loopStackEnd = nullptr;
    Vm* vm = nullptr;
    uint32_t slotsCacheOffset = 0;
    uint32_t definedCacheOffset = 0;
    uint32_t slotCountOffset = 0;
    uint32_t typeOffset = 0;
    uint32_t numberOffset = 0;
    uint32_t valueSize = 0;
};

// Generates native x86-64 code from bytecode for numeric-only functions.
// The generated code follows the platform C ABI:
//   int entry(Vm* vm, JitData* data);   // 0 = completed, 1 = bailout
class Jit {
public:
    explicit Jit(const Module& module);
    ~Jit();

    bool isCompiled(uint32_t funcIndex) const;
    int run(Vm* vm, uint32_t funcIndex) const;private:
    struct CompiledFunction {
        int (*entry)(Vm*, JitData*) = nullptr;
        JitData data = {};
        void* codeMemory = nullptr;
        std::size_t codeSize = 0;
        std::vector<double> valueStackStorage;
        std::vector<double> loopStackStorage;
    };

    static bool compileFunction(const Module& module, uint32_t funcIndex,
                                CompiledFunction& result);

    std::vector<CompiledFunction> functions_;
};

}  // namespace sek
