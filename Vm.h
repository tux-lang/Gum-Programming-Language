#pragma once

#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Bytecode.h"
#include "Telegram.h"
#include "Value.h"
#include "WindowsPlatform.h"

namespace sek {

class Jit;

class Vm {
public:
    explicit Vm(Module module);
    ~Vm();
    void run();

    // Runtime guard result consumed by the JIT (must stay first member).
    int jitResult_ = 0;
    std::vector<Value> slots_;
    std::vector<uint8_t> defined_;
    std::unordered_map<int, uint32_t> hotkeyIdMap_;
    // Cached direct-access views used by generated JIT code (refreshed after any
    // hotkey dispatch that could reallocate the vectors).
    const Value* jitSlotsCache_ = nullptr;
    const uint8_t* jitDefinedCache_ = nullptr;
    uint32_t jitSlotCountCache_ = 0;

    // Field layout offsets used by the JIT code generator.
    struct JitOffsets {
        std::size_t slotsCache;
        std::size_t definedCache;
        std::size_t slotCount;
    };
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static JitOffsets jitOffsets() {
        JitOffsets offsets;
        offsets.slotsCache = offsetof(Vm, jitSlotsCache_);
        offsets.definedCache = offsetof(Vm, jitDefinedCache_);
        offsets.slotCount = offsetof(Vm, jitSlotCountCache_);
        return offsets;
    }
#pragma GCC diagnostic pop

private:
    void runFunction(uint32_t funcIndex);
    Value callBuiltin(uint32_t builtinId, uint32_t argc, Value* args);
    void saveVariables(const std::string& path) const;
    void loadVariables(const std::string& path);
    void requestLoopStop(uint32_t loopId);
    bool consumeLoopStop(uint32_t loopId);
    void processPendingHotkeys();
    void runTelegramPolling();
    void dispatchTelegramUpdate(const TelegramUpdate& update);
    void executeTgCommand(uint32_t commandId, uint32_t argc);
    void runTgGroup(const std::string& name);
    void setTgGlobal(const std::string& name, const Value& value);
    void sendTelegramMessage(double chatId, const std::string& text, bool replyTo,
                             double replyToMessageId);
    double requireSleepDuration(const Value& value) const;
    double requireNumberArgument(const Value& value, const std::string& message) const;
    static std::mt19937_64 createSeededEngine();
    uint32_t slotOf(const std::string& name);

    Module module_;
    Telegram telegram_;
    double tgLastChat_ = 0;
    bool tgHasLastChat_ = false;
    double tgLastMessageId_ = 0;
    bool tgHasLastMessage_ = false;
    double tgLastBotMessageId_ = 0;
    double tgLastBotChat_ = 0;
    bool tgHasLastBotMessage_ = false;
    std::string tgLastCallbackId_;
    bool tgHasLastCallback_ = false;
    std::vector<Value> stack_;
    struct LoopCtx {
        double remaining;
        uint32_t id;
    };
    std::vector<LoopCtx> loopStack_;
    std::mt19937_64 randomEngine_;
    std::unordered_set<uint32_t> stoppedLoops_;
    bool jitInProgress_ = false;
    Jit* jit_ = nullptr;

    void push(const Value& value) { stack_.push_back(value); }
    Value pop() {
        Value value = std::move(stack_.back());
        stack_.pop_back();
        return value;
    }

    friend void jitStoreNumber(Vm* vm, uint32_t slot, uint64_t bits);
    friend void jitDefineNumber(Vm* vm, uint32_t slot, uint64_t bits);
    friend double jitLoadNumber(Vm* vm, uint32_t slot);
    friend void jitPollHotkeys(Vm* vm);
    friend bool jitConsumeLoopStop(Vm* vm, uint32_t id);
    friend void jitRequestLoopStop(Vm* vm, uint32_t id);
};

// JIT bridge helpers (called from generated native code).
void jitStoreNumber(Vm* vm, uint32_t slot, uint64_t bits);
void jitDefineNumber(Vm* vm, uint32_t slot, uint64_t bits);
double jitLoadNumber(Vm* vm, uint32_t slot);
void jitPollHotkeys(Vm* vm);
bool jitConsumeLoopStop(Vm* vm, uint32_t id);
void jitRequestLoopStop(Vm* vm, uint32_t id);
void jitPrintNumber(uint64_t bits);
void jitSleep(Vm* vm, uint64_t bits);
bool jitIsStopRequested();

}  // namespace sek
