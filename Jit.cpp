#include "Jit.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace sek {

namespace {

#ifdef _WIN32
constexpr bool kWindows = true;
#else
constexpr bool kWindows = false;
#endif

constexpr uint64_t kAbsMaskBits = 0x7FFFFFFFFFFFFFFFULL;
constexpr uint64_t kSignMaskBits = 0x8000000000000000ULL;
constexpr uint64_t kInfBits = 0x7FF0000000000000ULL;

uint64_t doubleBits(double value) {
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void* allocExec(std::size_t size) {
#ifdef _WIN32
    return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
    void* memory = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return memory == MAP_FAILED ? nullptr : memory;
#endif
}

void freeExec(void* memory, std::size_t size) {
    if (memory == nullptr) {
        return;
    }
#ifdef _WIN32
    VirtualFree(memory, 0, MEM_RELEASE);
#else
    munmap(memory, size);
#endif
}

enum Reg : uint8_t {
    R_RAX = 0,
    R_AL = 0,
    R_EAX = 0,
    R_RCX = 1,
    R_RDX = 2,
    R_RBX = 3,
    R_RSP = 4,
    R_RBP = 5,
    R_RSI = 6,
    R_RDI = 7,
    R_R8 = 8,
    R_R12 = 12,
    R_R13 = 13,
    R_R14 = 14,
    R_R15 = 15
};

enum Xmm : uint8_t {
    XMM0 = 0,
    XMM1 = 1,
    XMM2 = 2,
    XMM3 = 3
};

class Emitter {
public:
    explicit Emitter(std::vector<uint8_t>& buffer) : out_(buffer) {}

    uint32_t pos() const { return static_cast<uint32_t>(out_.size()); }

    void byte(uint8_t value) { out_.push_back(value); }

    void imm32(uint32_t value) {
        out_.push_back(static_cast<uint8_t>(value));
        out_.push_back(static_cast<uint8_t>(value >> 8));
        out_.push_back(static_cast<uint8_t>(value >> 16));
        out_.push_back(static_cast<uint8_t>(value >> 24));
    }

    void imm64(uint64_t value) {
        for (int index = 0; index < 8; ++index) {
            out_.push_back(static_cast<uint8_t>(value >> (8 * index)));
        }
    }

    void rex(bool r, bool x, bool b, bool w) {
        byte(0x40 | (w ? 0x08 : 0) | (r ? 0x04 : 0) | (x ? 0x02 : 0) | (b ? 0x01 : 0));
    }

    void movabs(Reg reg, uint64_t value) {
        rex(false, false, reg >= 8, true);
        byte(0xB8 + (reg & 7));
        imm64(value);
    }

    void mov32(Reg reg, uint32_t value) {
        if (reg >= 8) {
            rex(false, false, true, false);
        }
        byte(0xB8 + (reg & 7));
        imm32(value);
    }

    void movReg(Reg dst, Reg src) {
        rex(dst >= 8, false, src >= 8, true);
        byte(0x8B);
        byte(0xC0 | ((dst & 7) << 3) | (src & 7));
    }

    void movFromData(Reg reg, uint8_t displacement) {
        rex(reg >= 8, false, true, true);
        byte(0x8B);
        byte(0x40 | ((reg & 7) << 3) | 0x05);
        byte(displacement);
    }

    // mov r64, [r14 + disp32]   (r14 = vm pointer)
    void movFromVm(Reg reg, uint32_t displacement) {
        rex(reg >= 8, false, true, true);
        byte(0x8B);
        byte(0x80 | ((reg & 7) << 3) | 0x06);
        imm32(displacement);
    }

    // cmp eax, [r14 + disp32]
    void cmpEaxVmMem(uint32_t displacement) {
        byte(0x41);
        byte(0x3B);
        byte(0x86);
        imm32(displacement);
    }

    // movzx eax, byte [r15 + disp32]   (r15 = defined flags cache)
    void movzxEaxR15(uint32_t displacement) {
        byte(0x41);
        byte(0x0F);
        byte(0xB6);
        byte(0x87);
        imm32(displacement);
    }

    // cmp byte [rbp + disp32], imm8   (rbp = slots cache)
    void cmpByteRbp(uint32_t displacement, uint8_t immediate) {
        byte(0x80);
        byte(0xBD);
        imm32(displacement);
        byte(immediate);
    }

    // movsd xmm0, [rbp + disp32]
    void movsdXmm0Rbp(uint32_t displacement) {
        byte(0xF2);
        byte(0x0F);
        byte(0x10);
        byte(0x85);
        imm32(displacement);
    }

    // movsd [rbp + disp32], xmm0
    void movsdRbpXmm0(uint32_t displacement) {
        byte(0xF2);
        byte(0x0F);
        byte(0x11);
        byte(0x85);
        imm32(displacement);
    }

    // mov byte [rbp + disp32], imm8
    void movByteRbp(uint32_t displacement, uint8_t immediate) {
        byte(0xC6);
        byte(0x85);
        imm32(displacement);
        byte(immediate);
    }

    // mov byte [r15 + disp32], imm8
    void movByteR15(uint32_t displacement, uint8_t immediate) {
        byte(0x41);
        byte(0xC6);
        byte(0x87);
        imm32(displacement);
        byte(immediate);
    }

    void pushXmm(Xmm xmm) {
        subRegImm(R_RBX, 8);
        movsdToRbx(xmm);
    }

    void popToXmm(Xmm xmm) {
        movsdFromRbx(xmm);
        addRegImm(R_RBX, 8);
    }

    void movsdFromRbx(Xmm xmm) {
        byte(0xF2);
        byte(0x0F);
        byte(0x10);
        byte(0x00 | ((xmm & 7) << 3) | 0x03);
    }

    void movsdToRbx(Xmm xmm) {
        byte(0xF2);
        byte(0x0F);
        byte(0x11);
        byte(0x00 | ((xmm & 7) << 3) | 0x03);
    }

    void movsdReg(Xmm dst, Xmm src) {
        byte(0xF2);
        byte(0x0F);
        byte(0x10);
        byte(0xC0 | ((dst & 7) << 3) | (src & 7));
    }

    void movqGpToXmm(Xmm xmm, Reg gp) {
        byte(0x66);
        rex(xmm >= 8, false, gp >= 8, true);
        byte(0x0F);
        byte(0x6E);
        byte(0xC0 | ((xmm & 7) << 3) | (gp & 7));
    }

    void movqXmmToGp(Reg gp, Xmm xmm) {
        byte(0x66);
        rex(gp >= 8, false, xmm >= 8, true);
        byte(0x0F);
        byte(0x7E);
        byte(0xC0 | ((xmm & 7) << 3) | (gp & 7));
    }

    void pxorXmm(Xmm a, Xmm b) {
        byte(0x66);
        byte(0x0F);
        byte(0xEF);
        byte(0xC0 | ((a & 7) << 3) | (b & 7));
    }

    void xorpdXmm(Xmm a, Xmm b) {
        byte(0x66);
        byte(0x0F);
        byte(0x57);
        byte(0xC0 | ((a & 7) << 3) | (b & 7));
    }

    void andpdXmm(Xmm a, Xmm b) {
        byte(0x66);
        byte(0x0F);
        byte(0x54);
        byte(0xC0 | ((a & 7) << 3) | (b & 7));
    }

    void addsdXmm(Xmm a, Xmm b) {
        byte(0xF2);
        byte(0x0F);
        byte(0x58);
        byte(0xC0 | ((a & 7) << 3) | (b & 7));
    }

    void subsdXmm(Xmm a, Xmm b) {
        byte(0xF2);
        byte(0x0F);
        byte(0x5C);
        byte(0xC0 | ((a & 7) << 3) | (b & 7));
    }

    void mulsdXmm(Xmm a, Xmm b) {
        byte(0xF2);
        byte(0x0F);
        byte(0x59);
        byte(0xC0 | ((a & 7) << 3) | (b & 7));
    }

    void divsdXmm(Xmm a, Xmm b) {
        byte(0xF2);
        byte(0x0F);
        byte(0x5E);
        byte(0xC0 | ((a & 7) << 3) | (b & 7));
    }

    void ucomisdXmm(Xmm a, Xmm b) {
        byte(0x66);
        byte(0x0F);
        byte(0x2E);
        byte(0xC0 | ((a & 7) << 3) | (b & 7));
    }

    void roundsdXmm(Xmm dst, Xmm src, uint8_t immediate) {
        byte(0x66);
        byte(0x0F);
        byte(0x3A);
        byte(0x0B);
        byte(0xC0 | ((dst & 7) << 3) | (src & 7));
        byte(immediate);
    }

    void cvtsi2sdXmm(Xmm xmm, Reg gp) {
        byte(0xF2);
        byte(0x0F);
        byte(0x2A);
        byte(0xC0 | ((xmm & 7) << 3) | (gp & 7));
    }

    void addRegImm(Reg reg, uint8_t immediate) {
        rex(reg >= 8, false, false, true);
        byte(0x83);
        byte(0xC0 | (reg & 7));
        byte(immediate);
    }

    void subRegImm(Reg reg, uint8_t immediate) {
        rex(reg >= 8, false, false, true);
        byte(0x83);
        byte(0xE8 | (reg & 7));
        byte(immediate);
    }

    void setcc(uint8_t condition, Reg gp) {
        if (gp >= 8) {
            rex(false, false, true, false);
        }
        byte(0x0F);
        byte(0x90 + condition);
        byte(0xC0 | (gp & 7));
    }

    void movzx32(Reg dst, Reg src) {
        if (dst >= 8 || src >= 8) {
            rex(dst >= 8, false, src >= 8, false);
        }
        byte(0x0F);
        byte(0xB6);
        byte(0xC0 | ((dst & 7) << 3) | (src & 7));
    }

    void testReg(Reg a, Reg b) {
        if (a >= 8 || b >= 8) {
            rex(a >= 8, false, b >= 8, false);
        }
        byte(0x85);
        byte(0xC0 | ((a & 7) << 3) | (b & 7));
    }

    void testAl() {
        byte(0x84);
        byte(0xC0);
    }

    void callReg(Reg reg) {
        if (reg >= 8) {
            rex(false, false, true, false);
        }
        byte(0xFF);
        byte(0xD0 | (reg & 7));
    }

    uint32_t jmpRel32() {
        byte(0xE9);
        uint32_t field = pos();
        imm32(0);
        return field;
    }

    uint32_t jccRel32(uint8_t condition) {
        byte(0x0F);
        byte(0x80 + condition);
        uint32_t field = pos();
        imm32(0);
        return field;
    }

    void patchRel32(uint32_t field, uint32_t fromPos, uint32_t targetPos) {
        const uint32_t relative = targetPos - fromPos;
        out_[field] = static_cast<uint8_t>(relative);
        out_[field + 1] = static_cast<uint8_t>(relative >> 8);
        out_[field + 2] = static_cast<uint8_t>(relative >> 16);
        out_[field + 3] = static_cast<uint8_t>(relative >> 24);
    }

    void patchRel32To(uint32_t field, uint32_t targetPos) {
        patchRel32(field, field + 4, targetPos);
    }

    void prologue(uint32_t slotsCacheOffset, uint32_t definedCacheOffset) {
        byte(0x55);            // push rbp
        byte(0x53);            // push rbx
        byte(0x41);
        byte(0x54);            // push r12
        byte(0x41);
        byte(0x55);            // push r13
        byte(0x41);
        byte(0x56);            // push r14
        byte(0x41);
        byte(0x57);            // push r15
        subRegImm(R_RSP, 40);
        if (kWindows) {
            movReg(R_R14, R_RCX);
            movReg(R_R13, R_RDX);
        } else {
            movReg(R_R14, R_RDI);
            movReg(R_R13, R_RSI);
        }
        movFromData(R_RBX, 0);
        movFromData(R_R12, 16);
        movFromVm(R_RBP, slotsCacheOffset);
        movFromVm(R_R15, definedCacheOffset);
    }

    void reloadSlotCaches(uint32_t slotsCacheOffset, uint32_t definedCacheOffset) {
        movFromVm(R_RBP, slotsCacheOffset);
        movFromVm(R_R15, definedCacheOffset);
    }

    void epilogue(bool bailout) {
        addRegImm(R_RSP, 40);
        byte(0x41);
        byte(0x5F);            // pop r15
        byte(0x41);
        byte(0x5E);            // pop r14
        byte(0x41);
        byte(0x5D);            // pop r13
        byte(0x41);
        byte(0x5C);            // pop r12
        byte(0x5B);            // pop rbx
        byte(0x5D);            // pop rbp
        if (bailout) {
            mov32(R_RAX, 1);
        } else {
            mov32(R_RAX, 0);
        }
        byte(0xC3);            // ret
    }

    void callHelper(uint64_t address) {
        movabs(R_RAX, address);
        callReg(R_RAX);
    }

    void callPrint(uint64_t address) {
        if (kWindows) {
            movReg(R_RCX, R_RAX);
        } else {
            movReg(R_RDI, R_RAX);
        }
        callHelper(address);
    }

    void callSleep(uint64_t address) {
        if (kWindows) {
            movReg(R_RCX, R_R14);
            movReg(R_RDX, R_RAX);
        } else {
            movReg(R_RDI, R_R14);
            movReg(R_RSI, R_RAX);
        }
        callHelper(address);
    }

    void callPoll(uint64_t address) {
        if (kWindows) {
            movReg(R_RCX, R_R14);
        } else {
            movReg(R_RDI, R_R14);
        }
        callHelper(address);
    }

    void callConsume(uint64_t address) {
        if (kWindows) {
            movReg(R_RCX, R_R14);
            movReg(R_RDX, R_RAX);
        } else {
            movReg(R_RDI, R_R14);
            movReg(R_RSI, R_RAX);
        }
        callHelper(address);
    }

    void callRequest(uint64_t address, uint32_t id) {
        if (kWindows) {
            movReg(R_RCX, R_R14);
            mov32(R_RDX, id);
        } else {
            movReg(R_RDI, R_R14);
            mov32(R_RSI, id);
        }
        callHelper(address);
    }

    void callNoArg(uint64_t address) {
        callHelper(address);
    }

    // cmp dword [r14], 0 ; jne <patched to bailout>
    void bailIfResultSet(std::vector<uint32_t>& bailoutFields) {
        byte(0x41);
        byte(0x83);
        byte(0x3E);
        byte(0x00);
        uint32_t field = jccRel32(0x5);
        bailoutFields.push_back(field);
    }

private:
    std::vector<uint8_t>& out_;
};

struct JumpPatch {
    uint32_t field;
    uint32_t fromPos;
    uint32_t targetIp;
};

bool analyze(const std::vector<uint32_t>& code, std::size_t& maxDepth, std::size_t& maxLoops) {
    uint32_t ip = 0;
    std::ptrdiff_t depth = 0;
    std::size_t loopDepth = 0;
    maxDepth = 0;
    maxLoops = 0;

    while (ip < code.size()) {
        const uint32_t op = code[ip];
        switch (op) {
            case OP_CONST_NUM:
                depth += 1;
                ip += 2;
                break;
            case OP_LOAD:
                depth += 1;
                ip += 2;
                break;
            case OP_STORE:
            case OP_DEFINE:
                depth -= 1;
                ip += 2;
                break;
            case OP_ADD:
            case OP_SUB:
            case OP_MUL:
            case OP_DIV:
            case OP_EQUAL:
            case OP_NOT_EQUAL:
            case OP_GREATER:
            case OP_GREATER_EQUAL:
            case OP_LESS:
            case OP_LESS_EQUAL:
                depth -= 1;
                ip += 1;
                break;
            case OP_NEGATE:
            case OP_NOT:
                ip += 1;
                break;
            case OP_JUMP:
                ip = code[ip + 1];
                break;
            case OP_JUMP_IF_FALSE:
            case OP_JUMP_IF_TRUE:
                depth -= 1;
                ip += 2;
                break;
            case OP_POP:
            case OP_PRINT:
            case OP_SLEEP:
                depth -= 1;
                ip += 1;
                break;
            case OP_UNLOOP:
                ip += 2;
                break;
            case OP_LOOP: {
                const bool infinite = code[ip + 1] != 0;
                if (!infinite) {
                    depth -= 1;
                }
                loopDepth += 1;
                maxLoops = std::max(maxLoops, loopDepth);
                ip += 5;
                break;
            }
            case OP_LOOP_BACK:
                loopDepth -= 1;
                ip += 3;
                break;
            default:
                return false;
        }

        if (depth < 0) {
            return false;
        }
        maxDepth = std::max(maxDepth, static_cast<std::size_t>(depth));
    }

    return loopDepth == 0;
}

void emitSlotGuard(Emitter& e, uint32_t slot, uint32_t countOffset,
                   std::vector<uint32_t>& bailoutFields) {
    e.mov32(R_EAX, slot);
    e.cmpEaxVmMem(countOffset);
    uint32_t field = e.jccRel32(0x3);
    bailoutFields.push_back(field);
}

void emitLoadInline(Emitter& e, uint32_t slot, uint32_t countOffset, uint32_t typeOffset,
                    uint32_t numberOffset, uint32_t valueSize,
                    std::vector<uint32_t>& bailoutFields) {
    emitSlotGuard(e, slot, countOffset, bailoutFields);
    e.movzxEaxR15(slot);
    e.testReg(R_EAX, R_EAX);
    uint32_t definedField = e.jccRel32(0x4);
    bailoutFields.push_back(definedField);
    e.cmpByteRbp(slot * valueSize + typeOffset, 1);
    uint32_t typeField = e.jccRel32(0x5);
    bailoutFields.push_back(typeField);
    e.movsdXmm0Rbp(slot * valueSize + numberOffset);
    e.pushXmm(XMM0);
}

void emitStoreInline(Emitter& e, uint32_t slot, uint32_t countOffset, uint32_t typeOffset,
                     uint32_t numberOffset, uint32_t valueSize,
                     std::vector<uint32_t>& bailoutFields) {
    emitSlotGuard(e, slot, countOffset, bailoutFields);
    e.movzxEaxR15(slot);
    e.testReg(R_EAX, R_EAX);
    uint32_t definedField = e.jccRel32(0x4);
    bailoutFields.push_back(definedField);
    e.popToXmm(XMM0);
    e.movsdRbpXmm0(slot * valueSize + numberOffset);
    e.movByteRbp(slot * valueSize + typeOffset, 1);
}

void emitDefineInline(Emitter& e, uint32_t slot, uint32_t countOffset, uint32_t typeOffset,
                      uint32_t numberOffset, uint32_t valueSize,
                      std::vector<uint32_t>& bailoutFields) {
    emitSlotGuard(e, slot, countOffset, bailoutFields);
    e.popToXmm(XMM0);
    e.movsdRbpXmm0(slot * valueSize + numberOffset);
    e.movByteRbp(slot * valueSize + typeOffset, 1);
    e.movByteR15(slot, 1);
}

void emitCompare(Emitter& e, uint8_t condition) {
    e.popToXmm(XMM0);
    e.popToXmm(XMM1);
    e.ucomisdXmm(XMM1, XMM0);
    e.setcc(condition, R_AL);
    e.movzx32(R_EAX, R_AL);
    e.cvtsi2sdXmm(XMM0, R_EAX);
    e.pushXmm(XMM0);
}

}  // namespace

bool Jit::compileFunction(const Module& module, const uint32_t funcIndex,
                          CompiledFunction& result) {
    const std::vector<uint32_t>& code = module.functions[funcIndex].code;

    std::size_t maxDepth = 0;
    std::size_t maxLoops = 0;
    if (!analyze(code, maxDepth, maxLoops)) {
        return false;
    }

    const uint64_t printAddress = reinterpret_cast<uint64_t>(&jitPrintNumber);
    const uint64_t sleepAddress = reinterpret_cast<uint64_t>(&jitSleep);
    const uint64_t pollAddress = reinterpret_cast<uint64_t>(&jitPollHotkeys);
    const uint64_t consumeAddress = reinterpret_cast<uint64_t>(&jitConsumeLoopStop);
    const uint64_t requestAddress = reinterpret_cast<uint64_t>(&jitRequestLoopStop);
    const uint64_t stopAddress = reinterpret_cast<uint64_t>(&jitIsStopRequested);

    const Vm::JitOffsets vmOffsets = Vm::jitOffsets();
    const uint32_t slotsCacheOffset = static_cast<uint32_t>(vmOffsets.slotsCache);
    const uint32_t definedCacheOffset = static_cast<uint32_t>(vmOffsets.definedCache);
    const uint32_t slotCountOffset = static_cast<uint32_t>(vmOffsets.slotCount);
    const uint32_t typeOffset = static_cast<uint32_t>(Value::jitTypeOffset());
    const uint32_t numberOffset = static_cast<uint32_t>(Value::jitNumberOffset());
    const uint32_t valueSize = static_cast<uint32_t>(Value::jitSize());

    result.data.slotsCacheOffset = slotsCacheOffset;
    result.data.definedCacheOffset = definedCacheOffset;
    result.data.slotCountOffset = slotCountOffset;
    result.data.typeOffset = typeOffset;
    result.data.numberOffset = numberOffset;
    result.data.valueSize = valueSize;

    std::vector<uint8_t> buffer;
    Emitter e(buffer);
    std::vector<uint32_t> bailoutFields;
    std::vector<JumpPatch> jumpPatches;
    std::vector<uint32_t> offsets(code.size() + 1, 0);

    const uint64_t epsBits = doubleBits(1e-12);
    const uint64_t oneBits = doubleBits(1.0);

    e.prologue(slotsCacheOffset, definedCacheOffset);

    uint32_t ip = 0;
    while (ip < code.size()) {
        offsets[ip] = e.pos();
        const uint32_t op = code[ip];
        switch (op) {
            case OP_CONST_NUM: {
                const uint64_t bits = doubleBits(module.numbers[code[ip + 1]]);
                e.movabs(R_RAX, bits);
                e.movqGpToXmm(XMM1, R_RAX);
                e.pushXmm(XMM1);
                ip += 2;
                break;
            }
            case OP_LOAD:
                emitLoadInline(e, code[ip + 1], slotCountOffset, typeOffset, numberOffset,
                               valueSize, bailoutFields);
                ip += 2;
                break;
            case OP_STORE:
                emitStoreInline(e, code[ip + 1], slotCountOffset, typeOffset, numberOffset,
                                valueSize, bailoutFields);
                ip += 2;
                break;
            case OP_DEFINE:
                emitDefineInline(e, code[ip + 1], slotCountOffset, typeOffset, numberOffset,
                                 valueSize, bailoutFields);
                ip += 2;
                break;
            case OP_ADD:
                e.popToXmm(XMM0);
                e.popToXmm(XMM1);
                e.addsdXmm(XMM1, XMM0);
                e.pushXmm(XMM1);
                ip += 1;
                break;
            case OP_SUB:
                e.popToXmm(XMM0);
                e.popToXmm(XMM1);
                e.subsdXmm(XMM1, XMM0);
                e.pushXmm(XMM1);
                ip += 1;
                break;
            case OP_MUL:
                e.popToXmm(XMM0);
                e.popToXmm(XMM1);
                e.mulsdXmm(XMM1, XMM0);
                e.pushXmm(XMM1);
                ip += 1;
                break;
            case OP_DIV: {
                e.popToXmm(XMM0);
                e.popToXmm(XMM1);
                e.movsdReg(XMM2, XMM0);
                e.movabs(R_RAX, kAbsMaskBits);
                e.movqGpToXmm(XMM3, R_RAX);
                e.andpdXmm(XMM2, XMM3);
                e.movabs(R_RAX, epsBits);
                e.movqGpToXmm(XMM3, R_RAX);
                e.ucomisdXmm(XMM2, XMM3);
                uint32_t divField = e.jccRel32(0x2);
                bailoutFields.push_back(divField);
                e.divsdXmm(XMM1, XMM0);
                e.pushXmm(XMM1);
                ip += 1;
                break;
            }
            case OP_NEGATE:
                e.popToXmm(XMM0);
                e.movabs(R_RAX, kSignMaskBits);
                e.movqGpToXmm(XMM1, R_RAX);
                e.xorpdXmm(XMM0, XMM1);
                e.pushXmm(XMM0);
                ip += 1;
                break;
            case OP_NOT:
                e.popToXmm(XMM0);
                e.pxorXmm(XMM1, XMM1);
                e.ucomisdXmm(XMM0, XMM1);
                e.setcc(0x4, R_AL);
                e.movzx32(R_EAX, R_AL);
                e.cvtsi2sdXmm(XMM0, R_EAX);
                e.pushXmm(XMM0);
                ip += 1;
                break;
            case OP_EQUAL:
                emitCompare(e, 0x4);
                ip += 1;
                break;
            case OP_NOT_EQUAL:
                emitCompare(e, 0x5);
                ip += 1;
                break;
            case OP_GREATER:
                emitCompare(e, 0x7);
                ip += 1;
                break;
            case OP_GREATER_EQUAL:
                emitCompare(e, 0x3);
                ip += 1;
                break;
            case OP_LESS:
                emitCompare(e, 0x2);
                ip += 1;
                break;
            case OP_LESS_EQUAL:
                emitCompare(e, 0x6);
                ip += 1;
                break;
            case OP_JUMP: {
                const uint32_t target = code[ip + 1];
                const uint32_t field = e.jmpRel32();
                jumpPatches.push_back(JumpPatch{field, e.pos(), target});
                ip += 2;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                const uint32_t target = code[ip + 1];
                e.popToXmm(XMM0);
                e.pxorXmm(XMM1, XMM1);
                e.ucomisdXmm(XMM0, XMM1);
                const uint32_t skipField = e.jccRel32(0x5);
                const uint32_t jumpField = e.jmpRel32();
                const uint32_t continuePos = e.pos();
                e.patchRel32To(skipField, continuePos);
                jumpPatches.push_back(JumpPatch{jumpField, e.pos(), target});
                ip += 2;
                break;
            }
            case OP_JUMP_IF_TRUE: {
                const uint32_t target = code[ip + 1];
                e.popToXmm(XMM0);
                e.pxorXmm(XMM1, XMM1);
                e.ucomisdXmm(XMM0, XMM1);
                const uint32_t field = e.jccRel32(0x5);
                jumpPatches.push_back(JumpPatch{field, e.pos(), target});
                ip += 2;
                break;
            }
            case OP_POP:
                e.addRegImm(R_RBX, 8);
                ip += 1;
                break;
            case OP_PRINT:
                e.popToXmm(XMM0);
                e.movqXmmToGp(R_RAX, XMM0);
                e.callPrint(printAddress);
                ip += 1;
                break;
            case OP_SLEEP:
                e.popToXmm(XMM0);
                e.movqXmmToGp(R_RAX, XMM0);
                e.callSleep(sleepAddress);
                e.bailIfResultSet(bailoutFields);
                ip += 1;
                break;
            case OP_UNLOOP:
                e.callRequest(requestAddress, code[ip + 1]);
                ip += 2;
                break;
            case OP_LOOP: {
                const bool infinite = code[ip + 1] != 0;
                const uint32_t id = code[ip + 2];
                const uint32_t bodyStart = code[ip + 3];
                const uint32_t afterLoop = code[ip + 4];

                if (infinite) {
                    e.callNoArg(stopAddress);
                    e.testAl();
                    const uint32_t skipField = e.jccRel32(0x5);
                    jumpPatches.push_back(JumpPatch{skipField, e.pos(), afterLoop});
                } else {
                    e.popToXmm(XMM0);
                    e.pxorXmm(XMM1, XMM1);
                    e.ucomisdXmm(XMM0, XMM1);
                    const uint32_t negField = e.jccRel32(0x2);
                    bailoutFields.push_back(negField);
                    e.roundsdXmm(XMM1, XMM0, 0x09);
                }

                e.subRegImm(R_R12, 16);
                e.movabs(R_RAX, id);
                e.byte(0x49);
                e.byte(0x89);
                e.byte(0x04);
                e.byte(0x24);
                if (infinite) {
                    e.movabs(R_RAX, kInfBits);
                    e.movqGpToXmm(XMM1, R_RAX);
                }
                e.byte(0x41);
                e.byte(0xF2);
                e.byte(0x0F);
                e.byte(0x11);
                e.byte(0x4C);
                e.byte(0x24);
                e.byte(0x08);
                const uint32_t bodyField = e.jmpRel32();
                jumpPatches.push_back(JumpPatch{bodyField, e.pos(), bodyStart});
                ip += 5;
                break;
            }
            case OP_LOOP_BACK: {
                const uint32_t bodyStart = code[ip + 1];
                const uint32_t afterLoop = code[ip + 2];

                e.callNoArg(stopAddress);
                e.testAl();
                const uint32_t stopField = e.jccRel32(0x5);
                e.callPoll(pollAddress);
                e.reloadSlotCaches(slotsCacheOffset, definedCacheOffset);
                e.byte(0x41);
                e.byte(0x8B);
                e.byte(0x04);
                e.byte(0x24);
                e.testReg(R_EAX, R_EAX);
                const uint32_t skipConsumeField = e.jccRel32(0x4);
                e.callConsume(consumeAddress);
                e.testAl();
                const uint32_t consumedField = e.jccRel32(0x5);
                const uint32_t skipConsumePos = e.pos();

                e.byte(0x41);
                e.byte(0xF2);
                e.byte(0x0F);
                e.byte(0x10);
                e.byte(0x44);
                e.byte(0x24);
                e.byte(0x08);
                e.movabs(R_RAX, oneBits);
                e.movqGpToXmm(XMM1, R_RAX);
                e.subsdXmm(XMM0, XMM1);
                e.byte(0x41);
                e.byte(0xF2);
                e.byte(0x0F);
                e.byte(0x11);
                e.byte(0x44);
                e.byte(0x24);
                e.byte(0x08);
                e.pxorXmm(XMM1, XMM1);
                e.ucomisdXmm(XMM0, XMM1);
                const uint32_t exitField = e.jccRel32(0x6);
                const uint32_t loopField = e.jmpRel32();
                jumpPatches.push_back(JumpPatch{loopField, e.pos(), bodyStart});
                const uint32_t exitPos = e.pos();

                e.patchRel32To(stopField, exitPos);
                e.patchRel32To(skipConsumeField, skipConsumePos);
                e.patchRel32To(consumedField, exitPos);
                e.patchRel32To(exitField, exitPos);

                e.addRegImm(R_R12, 16);
                const uint32_t afterField = e.jmpRel32();
                jumpPatches.push_back(JumpPatch{afterField, e.pos(), afterLoop});
                ip += 3;
                break;
            }
            default:
                return false;
        }
    }
    offsets[code.size()] = e.pos();

    for (const JumpPatch& patch : jumpPatches) {
        if (patch.targetIp < offsets.size()) {
            e.patchRel32To(patch.field, offsets[patch.targetIp]);
        }
    }

    e.epilogue(false);

    const uint32_t bailoutPos = e.pos();
    for (const uint32_t field : bailoutFields) {
        e.patchRel32To(field, bailoutPos);
    }
    e.epilogue(true);

    void* memory = allocExec(buffer.size());
    if (memory == nullptr) {
        return false;
    }
    std::memcpy(memory, buffer.data(), buffer.size());

    result.entry = reinterpret_cast<int (*)(Vm*, JitData*)>(memory);
    result.codeMemory = memory;
    result.codeSize = buffer.size();
    result.valueStackStorage.assign(maxDepth, 0.0);
    result.loopStackStorage.assign(maxLoops * 2, 0.0);
    result.data.valueStack = result.valueStackStorage.data() + maxDepth;
    result.data.valueStackEnd = result.valueStackStorage.data();
    result.data.loopStack = result.loopStackStorage.data() + maxLoops * 2;
    result.data.loopStackEnd = result.loopStackStorage.data();

    return true;
}

Jit::Jit(const Module& module) {
    functions_.resize(module.functions.size());
    for (uint32_t index = 0; index < module.functions.size(); ++index) {
        compileFunction(module, index, functions_[index]);
    }
}

Jit::~Jit() {
    for (std::vector<CompiledFunction>::iterator it = functions_.begin();
         it != functions_.end(); ++it) {
        freeExec(it->codeMemory, it->codeSize);
    }
}

bool Jit::isCompiled(const uint32_t funcIndex) const {
    return funcIndex < functions_.size() && functions_[funcIndex].entry != nullptr;
}

int Jit::run(Vm* vm, const uint32_t funcIndex) const {
    if (funcIndex >= functions_.size() || functions_[funcIndex].entry == nullptr) {
        return 1;
    }

    const CompiledFunction& fn = functions_[funcIndex];
    vm->jitResult_ = 0;
    vm->jitSlotsCache_ = vm->slots_.data();
    vm->jitDefinedCache_ = vm->defined_.data();
    vm->jitSlotCountCache_ = static_cast<uint32_t>(vm->defined_.size());
    return fn.entry(vm, const_cast<JitData*>(&fn.data));
}

}  // namespace sek
