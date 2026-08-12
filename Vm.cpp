#include "Vm.h"

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <algorithm>

#ifdef _WIN32
#include <direct.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <filesystem>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#endif

#include "ConsoleControl.h"
#include "Error.h"
#include "Http.h"
#include "Jit.h"
#include "Json.h"

namespace sek {

namespace {

double requireNumber(const Value& value, const std::string& message) {
    if (!value.isNumber()) {
        throw RuntimeError(message);
    }

    return value.asNumber();
}

std::string requireString(const Value& value, const std::string& message) {
    if (!value.isString()) {
        throw RuntimeError(message);
    }

    return value.asString();
}

std::string typeName(const Value& value) {
    switch (value.type()) {
        case Value::Type::Nil:
            return "nil";
        case Value::Type::Number:
            return "number";
        case Value::Type::Boolean:
            return "boolean";
        case Value::Type::String:
            return "string";
        case Value::Type::Json:
            return "json";
    }

    return "unknown";
}

std::string escapeSaveString(const std::string& text) {
    std::string escaped;
    for (std::string::const_iterator it = text.begin(); it != text.end(); ++it) {
        switch (*it) {
            case '\\':
                escaped += "\\\\";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += *it;
                break;
        }
    }

    return escaped;
}

std::string unescapeSaveString(const std::string& text) {
    std::string unescaped;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '\\' || index + 1 >= text.size()) {
            unescaped += text[index];
            continue;
        }

        ++index;
        switch (text[index]) {
            case '\\':
                unescaped += '\\';
                break;
            case 'n':
                unescaped += '\n';
                break;
            case 'r':
                unescaped += '\r';
                break;
            case 't':
                unescaped += '\t';
                break;
            default:
                unescaped += text[index];
                break;
        }
    }

    return unescaped;
}

std::string serializeValue(const Value& value) {
    if (value.isNil()) {
        return "";
    }

    if (value.isNumber()) {
        std::ostringstream stream;
        stream << std::setprecision(17) << value.asNumber();
        return stream.str();
    }

    if (value.isBoolean()) {
        return value.asBoolean() ? "true" : "false";
    }

    return escapeSaveString(value.asString());
}

Value parseSavedValue(const std::string& type, const std::string& text) {
    if (type == "nil") {
        return Value();
    }

    if (type == "number") {
        try {
            return std::stod(text);
        } catch (const std::exception&) {
            throw RuntimeError("Invalid number in save file: " + text);
        }
    }

    if (type == "boolean") {
        if (text == "true") {
            return true;
        }

        if (text == "false") {
            return false;
        }

        throw RuntimeError("Invalid boolean in save file: " + text);
    }

    if (type == "string") {
        return unescapeSaveString(text);
    }

    throw RuntimeError("Unknown value type in save file: " + type);
}

std::string systemErrorMessage(const std::string& action, const std::string& path) {
    std::ostringstream stream;
    stream << action << ": " << path;
    if (errno != 0) {
        stream << " (" << std::strerror(errno) << ")";
    }

    return stream.str();
}

std::string windowsErrorMessage(const std::string& action, const std::string& path) {
#ifdef _WIN32
    const DWORD errorCode = GetLastError();
    LPSTR buffer = nullptr;
    const DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);

    std::string message;
    if (size > 0 && buffer != nullptr) {
        message.assign(buffer, size);
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r' ||
                                    message.back() == ' ' || message.back() == '\t')) {
            message.pop_back();
        }
    } else {
        message = "Windows error " + std::to_string(errorCode);
    }

    if (buffer != nullptr) {
        LocalFree(buffer);
    }

    return action + ": " + path + " (" + message + ")";
#else
    return systemErrorMessage(action, path);
#endif
}

void requireArgCount(const std::string& signature, const uint32_t argc, const uint32_t count) {
    if (argc != count) {
        throw RuntimeError(signature + " accepts exactly " + std::to_string(count) + " argument" +
                           (count == 1 ? "" : "s"));
    }
}

const char* tgCommandName(const uint32_t commandId) {
    switch (commandId) {
        case TG_TOKEN:
            return "tg_token";
        case TG_ON:
            return "tg_on";
        case TG_SEND:
            return "tg_send";
        case TG_REPLY:
            return "tg_reply";
        case TG_TYPING:
            return "tg_typing";
        case TG_PHOTO:
            return "tg_photo";
        case TG_STICKER:
            return "tg_sticker";
        case TG_DELETE:
            return "tg_delete";
        case TG_EDIT:
            return "tg_edit";
        case TG_CALLBACK_ANSWER:
            return "tg_callback_answer";
        case TG_GET_CHAT:
            return "tg_get_chat";
        case TG_LEAVE:
            return "tg_leave";
        default:
            return "tg_unknown";
    }
}

#ifdef _WIN32
DWORD pathAttributes(const std::string& path) {
    return GetFileAttributesA(path.c_str());
}

bool pathExists(const std::string& path) {
    return pathAttributes(path) != INVALID_FILE_ATTRIBUTES;
}

bool pathIsDirectory(const std::string& path) {
    const DWORD attributes = pathAttributes(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool pathIsFile(const std::string& path) {
    const DWORD attributes = pathAttributes(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}
#else
bool pathExists(const std::string& path) {
    struct stat info;
    return ::stat(path.c_str(), &info) == 0;
}

bool pathIsDirectory(const std::string& path) {
    struct stat info;
    return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool pathIsFile(const std::string& path) {
    struct stat info;
    return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}
#endif

void writeTextFile(const std::string& path, const std::string& text, const bool append) {
    std::ios::openmode mode = std::ios::binary | std::ios::out;
    mode |= append ? std::ios::app : std::ios::trunc;

    std::ofstream output(path.c_str(), mode);
    if (!output) {
        throw RuntimeError(systemErrorMessage("Cannot open file for writing", path));
    }

    output << text;
    if (!output) {
        throw RuntimeError(systemErrorMessage("Failed to write file", path));
    }
}

std::string readTextFile(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input) {
        throw RuntimeError(systemErrorMessage("Cannot open file for reading", path));
    }

    std::ostringstream stream;
    stream << input.rdbuf();
    if (input.bad()) {
        throw RuntimeError(systemErrorMessage("Failed to read file", path));
    }

    return stream.str();
}

void touchFile(const std::string& path) {
    std::ofstream output(path.c_str(), std::ios::binary | std::ios::app);
    if (!output) {
        throw RuntimeError(systemErrorMessage("Cannot create file", path));
    }
}

}  // namespace

Vm::Vm(Module module) : module_(std::move(module)), randomEngine_(createSeededEngine()) {
    const uint32_t slotCount = static_cast<uint32_t>(module_.slotNames.size());
    slots_.assign(slotCount, Value());
    defined_.assign(slotCount, 0);

    // Pre-define the telegram globals as nil so they read safely outside the
    // on_tg_* groups. Their slots are fixed before any JIT run.
    const char* tgGlobals[] = {"TG_CHAT_ID", "TG_TEXT", "TG_MESSAGE_ID",
                               "TG_CALLBACK_DATA", "TG_CALLBACK_ID"};
    for (const char* name : tgGlobals) {
        defined_[slotOf(name)] = 1;
    }

    stack_.reserve(1024);
    jit_ = new Jit(module_);
}

Vm::~Vm() {
    delete jit_;
}

void Vm::run() {
    const bool hasHotkeys = !module_.hotkeys.empty();

    std::vector<RegisteredHotkey> registeredHotkeys;
    if (hasHotkeys) {
        std::vector<std::string> triggers;
        triggers.reserve(module_.hotkeys.size());
        for (std::vector<std::pair<std::string, uint32_t>>::const_iterator it =
                 module_.hotkeys.begin();
             it != module_.hotkeys.end(); ++it) {
            triggers.push_back(it->first);
        }

        registeredHotkeys = registerHotkeys(triggers);
        hotkeyIdMap_.clear();
        for (std::size_t index = 0; index < registeredHotkeys.size(); ++index) {
            hotkeyIdMap_[registeredHotkeys[index].id] = module_.hotkeys[index].second;
        }
    }

    try {
        runFunction(0);

        // The Telegram token is set while the top-level script runs (tg_token),
        // so a bot can only be detected after the script has finished.
        const bool hasTelegram = telegram_.hasToken();

        if (!hasHotkeys && !hasTelegram) {
            return;
        }

        if (hasTelegram) {
            if (!isStopRequested()) {
                std::cout << "Listening for Telegram updates. Press Ctrl+C to stop.\n";
            }
            runTelegramPolling();
        } else {
            if (!isStopRequested()) {
                std::cout << "Listening for hotkeys. Press Ctrl+C to stop.\n";
            }

            while (!isStopRequested()) {
                const int hotkeyId = waitForHotkey();
                if (isStopRequested()) {
                    break;
                }

                const std::unordered_map<int, uint32_t>::const_iterator found =
                    hotkeyIdMap_.find(hotkeyId);
                if (found == hotkeyIdMap_.end()) {
                    continue;
                }

                runFunction(found->second);
            }
        }
    } catch (...) {
        unregisterHotkeys(registeredHotkeys);
        throw;
    }

    unregisterHotkeys(registeredHotkeys);
    hotkeyIdMap_.clear();
}

void Vm::runTelegramPolling() {
    while (!isStopRequested()) {
        std::vector<TelegramUpdate> updates;
        const int status = telegram_.fetchUpdates(updates);
        if (status < 0) {
            break;
        }

        if (status > 0) {
            preciseSleepMs(500);
            continue;
        }

        for (std::vector<TelegramUpdate>::const_iterator it = updates.begin();
             it != updates.end(); ++it) {
            if (isStopRequested()) {
                break;
            }

            dispatchTelegramUpdate(*it);
            processPendingHotkeys();
        }

        preciseSleepMs(500);
    }
}

void Vm::dispatchTelegramUpdate(const TelegramUpdate& update) {
    if (update.isCallback) {
        if (update.chatId != 0) {
            tgLastChat_ = update.chatId;
            tgHasLastChat_ = true;
            setTgGlobal("TG_CHAT_ID", Value(update.chatId));
        }

        if (update.messageId != 0) {
            setTgGlobal("TG_MESSAGE_ID", Value(update.messageId));
        }

        if (update.hasCallbackData) {
            setTgGlobal("TG_CALLBACK_DATA", Value(Telegram::textHash(update.callbackData)));
            tgLastCallbackId_ = update.callbackId;
            tgHasLastCallback_ = true;

            // The callback message becomes the target for tg_edit/tg_delete.
            tgLastBotMessageId_ = update.messageId;
            tgLastBotChat_ = update.chatId;
            tgHasLastBotMessage_ = true;
        }

        runTgGroup("on_tg_callback");
        return;
    }

    tgLastChat_ = update.chatId;
    tgHasLastChat_ = true;
    tgLastMessageId_ = update.messageId;
    tgHasLastMessage_ = true;
    setTgGlobal("TG_CHAT_ID", Value(update.chatId));
    setTgGlobal("TG_MESSAGE_ID", Value(update.messageId));

    if (update.hasText) {
        const double textHash = Telegram::textHash(update.text);
        setTgGlobal("TG_TEXT", Value(textHash));

        const std::string* reply = telegram_.staticReplyFor(textHash);
        if (reply != nullptr) {
            sendTelegramMessage(update.chatId, *reply, false, 0);
        }
    }

    runTgGroup("on_tg_message");
}

void Vm::runTgGroup(const std::string& name) {
    const std::unordered_map<std::string, uint32_t>::const_iterator found =
        module_.groupMap.find(name);
    if (found == module_.groupMap.end()) {
        return;
    }

    runFunction(found->second);
}

void Vm::setTgGlobal(const std::string& name, const Value& value) {
    const uint32_t slot = slotOf(name);
    slots_[slot] = value;
    defined_[slot] = 1;
}

void Vm::sendTelegramMessage(const double chatId, const std::string& text, const bool replyTo,
                             const double replyToMessageId) {
    double messageId = 0;
    if (!telegram_.sendMessage(chatId, text, replyTo, replyToMessageId, messageId)) {
        return;
    }

    if (messageId != 0) {
        tgLastBotMessageId_ = messageId;
    }
    tgLastBotChat_ = chatId;
    tgHasLastBotMessage_ = true;
    tgLastChat_ = chatId;
    tgHasLastChat_ = true;
}

void Vm::executeTgCommand(const uint32_t commandId, const uint32_t argc) {
    std::vector<Value> args;
    args.reserve(argc);
    for (uint32_t index = 0; index < argc; ++index) {
        args.push_back(pop());
    }
    std::reverse(args.begin(), args.end());

    const auto wrongArgCount = [&](const std::string& usage) {
        std::fprintf(stderr, "[gum] %s expects arguments: %s\n",
                     tgCommandName(commandId), usage.c_str());
    };

    switch (commandId) {
        case TG_TOKEN: {
            if (argc != 1) {
                wrongArgCount("tg_token <token>");
                break;
            }

            const std::string token = requireString(args[0], "tg_token expects a string token");
            if (!telegram_.setToken(token)) {
                throw RuntimeError("Invalid Telegram bot token (getMe failed)");
            }
            break;
        }
        case TG_ON: {
            if (argc != 2) {
                wrongArgCount("tg_on <command> <reply>");
                break;
            }

            const std::string command =
                requireString(args[0], "tg_on expects a string command");
            const std::string reply = requireString(args[1], "tg_on expects a string reply");
            telegram_.addStaticReply(Telegram::textHash(command), reply);
            break;
        }
        case TG_SEND: {
            if (argc == 1) {
                const std::string text = requireString(args[0], "tg_send expects a string text");
                if (!tgHasLastChat_) {
                    std::fprintf(stderr, "[gum] tg_send: no chat to send to\n");
                    break;
                }
                sendTelegramMessage(tgLastChat_, text, false, 0);
            } else if (argc == 2) {
                const double chatId =
                    requireNumber(args[0], "tg_send expects a numeric chat id");
                const std::string text = requireString(args[1], "tg_send expects a string text");
                sendTelegramMessage(chatId, text, false, 0);
            } else {
                wrongArgCount("tg_send <text> or tg_send <chat_id> <text>");
            }
            break;
        }
        case TG_REPLY: {
            if (argc != 1) {
                wrongArgCount("tg_reply <text>");
                break;
            }

            const std::string text = requireString(args[0], "tg_reply expects a string text");
            if (!tgHasLastChat_ || !tgHasLastMessage_) {
                std::fprintf(stderr, "[gum] tg_reply: no incoming message to reply to\n");
                break;
            }
            sendTelegramMessage(tgLastChat_, text, true, tgLastMessageId_);
            break;
        }
        case TG_TYPING: {
            if (argc != 0) {
                wrongArgCount("tg_typing");
                break;
            }

            if (!tgHasLastChat_) {
                std::fprintf(stderr, "[gum] tg_typing: no chat to show typing in\n");
                break;
            }
            telegram_.sendChatAction(tgLastChat_, "typing");
            break;
        }
        case TG_PHOTO:
        case TG_STICKER: {
            const bool isPhoto = commandId == TG_PHOTO;
            double chatId = 0;
            std::string file;
            if (argc == 1) {
                file = requireString(args[0], isPhoto ? "tg_photo expects a string file"
                                                      : "tg_sticker expects a string file");
                if (!tgHasLastChat_) {
                    std::fprintf(stderr, "[gum] %s: no chat to send to\n",
                                 tgCommandName(commandId));
                    break;
                }
                chatId = tgLastChat_;
            } else if (argc == 2) {
                chatId = requireNumber(args[0], isPhoto ? "tg_photo expects a numeric chat id"
                                                        : "tg_sticker expects a numeric chat id");
                file = requireString(args[1], isPhoto ? "tg_photo expects a string file"
                                                      : "tg_sticker expects a string file");
            } else {
                wrongArgCount(isPhoto ? "tg_photo <file> or tg_photo <chat_id> <file>"
                                      : "tg_sticker <file> or tg_sticker <chat_id> <file>");
                break;
            }

            const bool success = isPhoto ? telegram_.sendPhoto(chatId, file)
                                         : telegram_.sendSticker(chatId, file);
            if (success) {
                tgLastChat_ = chatId;
                tgHasLastChat_ = true;
            }
            break;
        }
        case TG_DELETE: {
            double chatId = tgLastBotChat_;
            double messageId = tgLastBotMessageId_;
            if (argc == 0) {
                if (!tgHasLastBotMessage_) {
                    std::fprintf(stderr, "[gum] tg_delete: no bot message to delete\n");
                    break;
                }
            } else if (argc == 1) {
                if (!tgHasLastBotMessage_) {
                    std::fprintf(stderr, "[gum] tg_delete: no bot message to delete\n");
                    break;
                }
                chatId = requireNumber(args[0], "tg_delete expects a numeric chat id");
            } else if (argc == 2) {
                chatId = requireNumber(args[0], "tg_delete expects a numeric chat id");
                messageId = requireNumber(args[1], "tg_delete expects a numeric message id");
            } else {
                wrongArgCount("tg_delete, tg_delete <chat_id> or tg_delete <chat_id> <message_id>");
                break;
            }

            telegram_.deleteMessage(chatId, messageId);
            break;
        }
        case TG_EDIT: {
            if (argc != 1) {
                wrongArgCount("tg_edit <new_text>");
                break;
            }

            const std::string text = requireString(args[0], "tg_edit expects a string text");
            if (!tgHasLastBotMessage_) {
                std::fprintf(stderr, "[gum] tg_edit: no bot message to edit\n");
                break;
            }
            telegram_.editMessageText(tgLastBotChat_, tgLastBotMessageId_, text);
            break;
        }
        case TG_CALLBACK_ANSWER: {
            if (argc != 1) {
                wrongArgCount("tg_callback_answer <text>");
                break;
            }

            const std::string text =
                requireString(args[0], "tg_callback_answer expects a string text");
            if (!tgHasLastCallback_) {
                std::fprintf(stderr, "[gum] tg_callback_answer: no callback to answer\n");
                break;
            }
            telegram_.answerCallbackQuery(tgLastCallbackId_, text);
            break;
        }
        case TG_GET_CHAT: {
            if (argc != 1) {
                wrongArgCount("tg_get_chat <chat_id>");
                break;
            }

            const double chatId =
                requireNumber(args[0], "tg_get_chat expects a numeric chat id");
            telegram_.getChat(chatId);
            break;
        }
        case TG_LEAVE: {
            if (argc != 1) {
                wrongArgCount("tg_leave <chat_id>");
                break;
            }

            const double chatId = requireNumber(args[0], "tg_leave expects a numeric chat id");
            telegram_.leaveChat(chatId);
            break;
        }
        default:
            break;
    }
}

void Vm::processPendingHotkeys() {
    if (hotkeyIdMap_.empty()) {
        return;
    }

    while (!isStopRequested()) {
        const int hotkeyId = pollHotkey();
        if (hotkeyId < 0) {
            return;
        }

        const std::unordered_map<int, uint32_t>::const_iterator found =
            hotkeyIdMap_.find(hotkeyId);
        if (found == hotkeyIdMap_.end()) {
            continue;
        }

        runFunction(found->second);
    }
}

void Vm::runFunction(uint32_t funcIndex) {
    if (jit_ != nullptr && !jitInProgress_ && jit_->isCompiled(funcIndex)) {
        jitInProgress_ = true;
        const int status = jit_->run(this, funcIndex);
        jitInProgress_ = false;
        if (status == 0) {
            return;
        }
    }

    const Function& fn = module_.functions[funcIndex];
    const std::vector<uint32_t>& code = fn.code;
    const uint32_t codeSize = static_cast<uint32_t>(code.size());
    uint32_t ip = 0;

    for (;;) {
        if (ip >= codeSize) {
            break;
        }

        const uint32_t op = code[ip];
        switch (op) {
            case OP_CONST_NUM:
                push(Value(module_.numbers[code[ip + 1]]));
                ip += 2;
                break;
            case OP_CONST_STR:
                push(Value(module_.strings[code[ip + 1]]));
                ip += 2;
                break;
            case OP_CONST_TRUE:
                push(Value(true));
                ip += 1;
                break;
            case OP_CONST_FALSE:
                push(Value(false));
                ip += 1;
                break;
            case OP_CONST_NIL:
                push(Value());
                ip += 1;
                break;
            case OP_LOAD: {
                const uint32_t slot = code[ip + 1];
                if (!defined_[slot]) {
                    throw RuntimeError("Undefined variable: " + module_.slotNames[slot]);
                }
                push(slots_[slot]);
                ip += 2;
                break;
            }
            case OP_STORE: {
                const uint32_t slot = code[ip + 1];
                if (!defined_[slot]) {
                    throw RuntimeError("Undefined variable: " + module_.slotNames[slot]);
                }
                slots_[slot] = pop();
                ip += 2;
                break;
            }
            case OP_DEFINE: {
                const uint32_t slot = code[ip + 1];
                slots_[slot] = pop();
                defined_[slot] = 1;
                ip += 2;
                break;
            }
            case OP_TOGGLE: {
                const uint32_t slot = code[ip + 1];
                if (!defined_[slot]) {
                    throw RuntimeError("Undefined variable: " + module_.slotNames[slot]);
                }
                Value& value = slots_[slot];
                if (!value.isBoolean()) {
                    throw RuntimeError("toggle expects a boolean variable: " + module_.slotNames[slot]);
                }
                value = Value(!value.asBoolean());
                ip += 2;
                break;
            }
            case OP_ADD: {
                const Value b = pop();
                const Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(Value(a.asNumber() + b.asNumber()));
                } else {
                    push(Value(valueToString(a) + valueToString(b)));
                }
                ip += 1;
                break;
            }
            case OP_SUB: {
                const Value b = pop();
                const Value a = pop();
                push(Value(requireNumber(a, "Left side of '-' must be a number") -
                           requireNumber(b, "Right side of '-' must be a number")));
                ip += 1;
                break;
            }
            case OP_MUL: {
                const Value b = pop();
                const Value a = pop();
                push(Value(requireNumber(a, "Left side of '*' must be a number") *
                           requireNumber(b, "Right side of '*' must be a number")));
                ip += 1;
                break;
            }
            case OP_DIV: {
                const Value b = pop();
                const Value a = pop();
                const double divisor = requireNumber(b, "Right side of '/' must be a number");
                if (std::fabs(divisor) < 1e-12) {
                    throw RuntimeError("Division by zero");
                }
                push(Value(requireNumber(a, "Left side of '/' must be a number") / divisor));
                ip += 1;
                break;
            }
            case OP_NEGATE: {
                const Value value = pop();
                push(Value(-requireNumber(value, "Unary '-' expects a number")));
                ip += 1;
                break;
            }
            case OP_NOT: {
                const Value value = pop();
                push(Value(!isTruthy(value)));
                ip += 1;
                break;
            }
            case OP_EQUAL: {
                const Value b = pop();
                const Value a = pop();
                push(Value(a == b));
                ip += 1;
                break;
            }
            case OP_NOT_EQUAL: {
                const Value b = pop();
                const Value a = pop();
                push(Value(!(a == b)));
                ip += 1;
                break;
            }
            case OP_GREATER: {
                const Value b = pop();
                const Value a = pop();
                push(Value(requireNumber(a, "Left side of '>' must be a number") >
                           requireNumber(b, "Right side of '>' must be a number")));
                ip += 1;
                break;
            }
            case OP_GREATER_EQUAL: {
                const Value b = pop();
                const Value a = pop();
                push(Value(requireNumber(a, "Left side of '>=' must be a number") >=
                           requireNumber(b, "Right side of '>=' must be a number")));
                ip += 1;
                break;
            }
            case OP_LESS: {
                const Value b = pop();
                const Value a = pop();
                push(Value(requireNumber(a, "Left side of '<' must be a number") <
                           requireNumber(b, "Right side of '<' must be a number")));
                ip += 1;
                break;
            }
            case OP_LESS_EQUAL: {
                const Value b = pop();
                const Value a = pop();
                push(Value(requireNumber(a, "Left side of '<=' must be a number") <=
                           requireNumber(b, "Right side of '<=' must be a number")));
                ip += 1;
                break;
            }
            case OP_JUMP:
                ip = code[ip + 1];
                break;
            case OP_JUMP_IF_FALSE: {
                const Value value = pop();
                if (isTruthy(value)) {
                    ip += 2;
                } else {
                    ip = code[ip + 1];
                }
                break;
            }
            case OP_JUMP_IF_TRUE: {
                const Value value = pop();
                if (isTruthy(value)) {
                    ip = code[ip + 1];
                } else {
                    ip += 2;
                }
                break;
            }
            case OP_POP:
                pop();
                ip += 1;
                break;
            case OP_PRINT:
                std::cout << valueToString(pop()) << '\n';
                ip += 1;
                break;
            case OP_MSG:
                showMessageBox(valueToString(pop()));
                ip += 1;
                break;
            case OP_PLAY_SOUND:
                playSoundFile(requireString(pop(), "playsound expects a string file path"));
                ip += 1;
                break;
            case OP_SEND_TEXT:
                sendText(requireString(pop(), "send text parts must be strings"));
                ip += 1;
                break;
            case OP_SEND_KEY:
                sendNamedKey(module_.strings[code[ip + 1]]);
                ip += 2;
                break;
            case OP_SAVE:
                saveVariables(requireString(pop(), "save expects a string file path"));
                ip += 1;
                break;
            case OP_LOAD_FILE:
                loadVariables(requireString(pop(), "load expects a string file path"));
                ip += 1;
                break;
            case OP_CLICK:
                clickLeftMouseButton();
                ip += 1;
                break;
            case OP_MOUSE_DOWN:
                leftMouseDown();
                ip += 1;
                break;
            case OP_MOUSE_UP:
                leftMouseUp();
                ip += 1;
                break;
            case OP_MOUSE_MOVE: {
                const Value yValue = pop();
                const Value xValue = pop();
                moveMouse(
                    static_cast<int>(std::lround(requireNumberArgument(
                        xValue, "mousemove expects numeric X coordinate"))),
                    static_cast<int>(std::lround(requireNumberArgument(
                        yValue, "mousemove expects numeric Y coordinate"))));
                ip += 1;
                break;
            }
            case OP_MOUSE_HOLD: {
                const Value duration = pop();
                leftMouseDown();
                try {
                    preciseSleepMs(requireSleepDuration(duration));
                    leftMouseUp();
                } catch (...) {
                    leftMouseUp();
                    throw;
                }
                ip += 1;
                break;
            }
            case OP_SLEEP:
                preciseSleepMs(requireSleepDuration(pop()));
                ip += 1;
                break;
            case OP_UNLOOP:
                requestLoopStop(code[ip + 1]);
                ip += 2;
                break;
            case OP_LOOP: {
                const bool infinite = code[ip + 1] != 0;
                const uint32_t id = code[ip + 2];
                const uint32_t bodyStart = code[ip + 3];
                const uint32_t afterLoop = code[ip + 4];

                double remaining = std::numeric_limits<double>::infinity();
                if (!infinite) {
                    const Value countValue = pop();
                    const double count =
                        requireNumberArgument(countValue, "loop expects a numeric repeat count");
                    if (count < 0.0) {
                        throw RuntimeError("loop count cannot be negative");
                    }
                    remaining = std::floor(count);
                }

                if (infinite && isStopRequested()) {
                    ip = afterLoop;
                    break;
                }

                loopStack_.push_back(LoopCtx{remaining, id});
                ip = bodyStart;
                break;
            }
            case OP_LOOP_BACK: {
                const uint32_t bodyStart = code[ip + 1];
                const uint32_t afterLoop = code[ip + 2];

                const LoopCtx ctx = loopStack_.back();
                if (isStopRequested()) {
                    loopStack_.pop_back();
                    ip = afterLoop;
                    break;
                }

                processPendingHotkeys();

                if (ctx.id != 0 && consumeLoopStop(ctx.id)) {
                    loopStack_.pop_back();
                    ip = afterLoop;
                    break;
                }

                double remaining = ctx.remaining - 1.0;
                if (remaining > 0.0) {
                    loopStack_.back().remaining = remaining;
                    ip = bodyStart;
                } else {
                    loopStack_.pop_back();
                    ip = afterLoop;
                }
                break;
            }
            case OP_CALL_BUILTIN: {
                const uint32_t builtinId = code[ip + 1];
                const uint32_t argc = code[ip + 2];
                if (argc > stack_.size()) {
                    throw RuntimeError("Internal error: argument stack underflow");
                }
                Value* args = stack_.data() + (stack_.size() - argc);
                Value result = callBuiltin(builtinId, argc, args);
                stack_.resize(stack_.size() - argc);
                push(result);
                ip += 3;
                break;
            }
            case OP_CALL_GROUP: {
                const std::string& name = module_.strings[code[ip + 1]];
                const uint32_t argc = code[ip + 2];

                const std::unordered_map<std::string, uint32_t>::const_iterator found =
                    module_.groupMap.find(name);
                if (found == module_.groupMap.end()) {
                    throw RuntimeError("Unknown function or group: " + name);
                }
                if (argc != 0) {
                    throw RuntimeError("Group '" + name + "' does not accept arguments");
                }

                stack_.resize(stack_.size() - argc);
                runFunction(found->second);
                push(Value());
                ip += 3;
                break;
            }
            case OP_TG_CALL: {
                const uint32_t commandId = code[ip + 1];
                const uint32_t argc = code[ip + 2];
                if (argc > stack_.size()) {
                    throw RuntimeError("Internal error: argument stack underflow");
                }
                executeTgCommand(commandId, argc);
                ip += 3;
                break;
            }
            default:
                throw RuntimeError("Unknown bytecode opcode during execution");
        }
    }
}

Value Vm::callBuiltin(uint32_t builtinId, uint32_t argc, Value* args) {
    const auto stringArgument = [&](const uint32_t index, const std::string& signature) {
        return requireString(args[index], signature + " expects string arguments");
    };

    switch (builtinId) {
        case B_INPUT: {
            if (argc > 1) {
                throw RuntimeError("input(...) accepts zero or one argument");
            }

            if (argc == 1) {
                std::cout << valueToString(args[0]);
            }

            std::string line;
            std::getline(std::cin, line);
            return line;
        }
        case B_RANDOM: {
            if (argc == 0) {
                std::uniform_real_distribution<double> distribution(
                    0.0, std::nextafter(1.0, std::numeric_limits<double>::max()));
                return distribution(randomEngine_);
            }

            if (argc == 1) {
                const double maxValue =
                    requireNumberArgument(args[0], "random(max) expects a numeric max value");
                if (maxValue < 0.0) {
                    throw RuntimeError("random(max) requires max >= 0");
                }

                std::uniform_int_distribution<int> distribution(
                    0, static_cast<int>(std::floor(maxValue)));
                return static_cast<double>(distribution(randomEngine_));
            }

            if (argc == 2) {
                const double minValue =
                    requireNumberArgument(args[0], "random(min, max) expects a numeric min value");
                const double maxValue =
                    requireNumberArgument(args[1], "random(min, max) expects a numeric max value");
                if (maxValue < minValue) {
                    throw RuntimeError("random(min, max) requires max >= min");
                }

                std::uniform_int_distribution<int> distribution(
                    static_cast<int>(std::floor(minValue)),
                    static_cast<int>(std::floor(maxValue)));
                return static_cast<double>(distribution(randomEngine_));
            }

            throw RuntimeError("random() accepts 0, 1, or 2 arguments");
        }
        case B_SEED: {
            if (argc > 1) {
                throw RuntimeError("seed() accepts zero or one argument");
            }

            if (argc == 0) {
                randomEngine_ = createSeededEngine();
                return Value();
            }

            const double seedValue =
                requireNumberArgument(args[0], "seed(value) expects a numeric value");
            randomEngine_.seed(static_cast<std::mt19937_64::result_type>(seedValue));
            return Value();
        }
        case B_TYPE: {
            requireArgCount("type(value)", argc, 1);
            return typeName(args[0]);
        }
        case B_ASSERT: {
            if (argc == 0 || argc > 2) {
                throw RuntimeError("assert(condition, message) accepts one or two arguments");
            }

            if (isTruthy(args[0])) {
                return true;
            }

            std::string message = "Assertion failed";
            if (argc == 2) {
                message = valueToString(args[1]);
            }

            throw RuntimeError(message);
        }
        case B_EXISTS:
            requireArgCount("exists(path)", argc, 1);
            return pathExists(stringArgument(0, "exists(path)"));
        case B_ISFILE:
            requireArgCount("isfile(path)", argc, 1);
            return pathIsFile(stringArgument(0, "isfile(path)"));
        case B_ISDIR:
            requireArgCount("isdir(path)", argc, 1);
            return pathIsDirectory(stringArgument(0, "isdir(path)"));
        case B_TOUCH:
            requireArgCount("touch(path)", argc, 1);
            touchFile(stringArgument(0, "touch(path)"));
            return true;
        case B_WRITE:
            requireArgCount("write(path, text)", argc, 2);
            writeTextFile(stringArgument(0, "write(path, text)"),
                          stringArgument(1, "write(path, text)"), false);
            return true;
        case B_APPEND:
            requireArgCount("append(path, text)", argc, 2);
            writeTextFile(stringArgument(0, "append(path, text)"),
                          stringArgument(1, "append(path, text)"), true);
            return true;
        case B_READ:
            requireArgCount("read(path)", argc, 1);
            return readTextFile(stringArgument(0, "read(path)"));
        case B_REMOVE: {
            requireArgCount("remove(path)", argc, 1);
            const std::string path = stringArgument(0, "remove(path)");
            errno = 0;
            if (std::remove(path.c_str()) != 0) {
                throw RuntimeError(systemErrorMessage("Cannot remove file", path));
            }
            return true;
        }
        case B_MKDIR: {
            requireArgCount("mkdir(path)", argc, 1);
            const std::string path = stringArgument(0, "mkdir(path)");
            if (pathIsDirectory(path)) {
                return true;
            }

            errno = 0;
#ifdef _WIN32
            if (_mkdir(path.c_str()) != 0) {
                throw RuntimeError(systemErrorMessage("Cannot create directory", path));
            }
#else
            if (::mkdir(path.c_str(), 0755) != 0) {
                throw RuntimeError(systemErrorMessage("Cannot create directory", path));
            }
#endif
            return true;
        }
        case B_RMDIR: {
            requireArgCount("rmdir(path)", argc, 1);
            const std::string path = stringArgument(0, "rmdir(path)");
            errno = 0;
#ifdef _WIN32
            if (_rmdir(path.c_str()) != 0) {
                throw RuntimeError(systemErrorMessage("Cannot remove directory", path));
            }
#else
            if (::rmdir(path.c_str()) != 0) {
                throw RuntimeError(systemErrorMessage("Cannot remove directory", path));
            }
#endif
            return true;
        }
        case B_RENAME: {
            requireArgCount("rename(from, to)", argc, 2);
            const std::string from = stringArgument(0, "rename(from, to)");
            const std::string to = stringArgument(1, "rename(from, to)");
#ifdef _WIN32
            if (!MoveFileExA(from.c_str(), to.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
                throw RuntimeError(windowsErrorMessage("Cannot rename path", from + " -> " + to));
            }
#else
            errno = 0;
            if (::rename(from.c_str(), to.c_str()) != 0) {
                throw RuntimeError(systemErrorMessage("Cannot rename path", from + " -> " + to));
            }
#endif
            return true;
        }
        case B_COPY: {
            requireArgCount("copy(from, to)", argc, 2);
            const std::string from = stringArgument(0, "copy(from, to)");
            const std::string to = stringArgument(1, "copy(from, to)");
#ifdef _WIN32
            if (!CopyFileA(from.c_str(), to.c_str(), FALSE)) {
                throw RuntimeError(windowsErrorMessage("Cannot copy file", from + " -> " + to));
            }
#else
            errno = 0;
            try {
                if (std::filesystem::copy_file(from, to,
                        std::filesystem::copy_options::overwrite_existing) == false) {
                    throw std::runtime_error("copy failed");
                }
            } catch (const std::exception&) {
                throw RuntimeError(systemErrorMessage("Cannot copy file", from + " -> " + to));
            }
#endif
            return true;
        }
        case B_HTTP_GET: {
            requireArgCount("http_get(url)", argc, 1);
            return httpGet(stringArgument(0, "http_get(url)"));
        }
        case B_HTTP_POST: {
            if (argc != 2 && argc != 3) {
                throw RuntimeError("http_post(url, body, content_type) accepts 2 or 3 arguments");
            }

            const std::string url = stringArgument(0, "http_post(url, body, content_type)");
            const std::string body = stringArgument(1, "http_post(url, body, content_type)");
            const std::string contentType = argc == 3
                ? stringArgument(2, "http_post(url, body, content_type)")
                : "text/plain";
            return httpPost(url, body, contentType);
        }
        case B_JSON_PARSE: {
            requireArgCount("json_parse(str)", argc, 1);
            const int handle = jsonParse(stringArgument(0, "json_parse(str)"));
            return handle == 0 ? Value() : Value::makeJson(handle);
        }
        case B_JSON_GET: {
            requireArgCount("json_get(obj, key)", argc, 2);
            if (!args[0].isJson()) {
                return Value();
            }

            return jsonGet(args[0].asJsonHandle(), stringArgument(1, "json_get(obj, key)"));
        }
        case B_JSON_ARRAY_LENGTH: {
            requireArgCount("json_array_length(arr)", argc, 1);
            if (!args[0].isJson()) {
                return Value();
            }

            return jsonArrayLength(args[0].asJsonHandle());
        }
        case B_JSON_ARRAY_GET: {
            requireArgCount("json_array_get(arr, index)", argc, 2);
            if (!args[0].isJson()) {
                return Value();
            }

            const double indexValue =
                requireNumberArgument(args[1], "json_array_get(arr, index) expects a numeric index");
            return jsonArrayGet(args[0].asJsonHandle(), static_cast<int>(indexValue));
        }
        case B_JSON_TYPE: {
            requireArgCount("json_type(val)", argc, 1);
            const Value& value = args[0];
            if (value.isJson()) {
                return jsonTypeName(value.asJsonHandle());
            }
            if (value.isNil()) {
                return "null";
            }
            if (value.isNumber()) {
                return "number";
            }
            if (value.isBoolean()) {
                return "boolean";
            }
            return "string";
        }
        case B_TG_CMD_HASH: {
            requireArgCount("tg_cmd_hash(text)", argc, 1);
            return Telegram::textHash(requireString(args[0], "tg_cmd_hash expects a string"));
        }
    }

    throw RuntimeError("Unknown function");
}

uint32_t Vm::slotOf(const std::string& name) {
    const std::unordered_map<std::string, uint32_t>::const_iterator found =
        module_.nameToSlot.find(name);
    if (found != module_.nameToSlot.end()) {
        return found->second;
    }

    const uint32_t slot = static_cast<uint32_t>(slots_.size());
    module_.nameToSlot[name] = slot;
    module_.slotNames.push_back(name);
    slots_.push_back(Value());
    defined_.push_back(0);
    return slot;
}

void Vm::saveVariables(const std::string& path) const {
    std::ofstream output(path.c_str(), std::ios::binary);
    if (!output) {
        throw RuntimeError("Cannot write save file: " + path);
    }

    output << "SEKLANG_SAVE_V1\n";
    for (std::unordered_map<std::string, uint32_t>::const_iterator it =
             module_.nameToSlot.begin();
         it != module_.nameToSlot.end(); ++it) {
        if (it->second >= defined_.size() || !defined_[it->second]) {
            continue;
        }

        // JSON handles are runtime-only and cannot be restored from a save file.
        if (slots_[it->second].isJson()) {
            continue;
        }

        output << it->first << '\t' << typeName(slots_[it->second]) << '\t'
               << serializeValue(slots_[it->second]) << '\n';
    }

    if (!output) {
        throw RuntimeError("Failed to write save file: " + path);
    }
}

void Vm::loadVariables(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input) {
        throw RuntimeError("Cannot open save file: " + path);
    }

    std::string line;
    if (!std::getline(input, line) || line != "SEKLANG_SAVE_V1") {
        throw RuntimeError("Invalid save file: " + path);
    }

    int lineNumber = 1;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (line.empty()) {
            continue;
        }

        const std::string::size_type firstTab = line.find('\t');
        const std::string::size_type secondTab =
            firstTab == std::string::npos ? std::string::npos : line.find('\t', firstTab + 1);
        if (firstTab == std::string::npos || secondTab == std::string::npos) {
            throw RuntimeError("Invalid save entry on line " + std::to_string(lineNumber));
        }

        const std::string name = line.substr(0, firstTab);
        const std::string type = line.substr(firstTab + 1, secondTab - firstTab - 1);
        const std::string valueText = line.substr(secondTab + 1);
        if (name.empty()) {
            throw RuntimeError("Invalid empty variable name in save file on line " +
                               std::to_string(lineNumber));
        }

        const uint32_t slot = slotOf(name);
        slots_[slot] = parseSavedValue(type, valueText);
        defined_[slot] = 1;
    }
}

void Vm::requestLoopStop(const uint32_t loopId) {
    stoppedLoops_.insert(loopId);
}

bool Vm::consumeLoopStop(const uint32_t loopId) {
    return stoppedLoops_.erase(loopId) > 0;
}

double Vm::requireSleepDuration(const Value& value) const {
    if (!value.isNumber()) {
        throw RuntimeError("sleep expects a number in milliseconds");
    }

    if (value.asNumber() < 0.0) {
        throw RuntimeError("sleep duration cannot be negative");
    }

    return value.asNumber();
}

double Vm::requireNumberArgument(const Value& value, const std::string& message) const {
    if (!value.isNumber()) {
        throw RuntimeError(message);
    }

    return value.asNumber();
}

std::mt19937_64 Vm::createSeededEngine() {
#ifdef _WIN32
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);

    const unsigned long long timeSeed = static_cast<unsigned long long>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const unsigned long long tickSeed = static_cast<unsigned long long>(GetTickCount());
    const unsigned long long pidSeed = static_cast<unsigned long long>(GetCurrentProcessId());
    const unsigned long long tidSeed = static_cast<unsigned long long>(GetCurrentThreadId());
    const unsigned long long perfSeed = static_cast<unsigned long long>(counter.QuadPart);
    const unsigned long long randomDeviceSeed =
        static_cast<unsigned long long>(std::random_device{}());

    std::vector<unsigned int> seedValues;
    seedValues.push_back(static_cast<unsigned int>(timeSeed));
    seedValues.push_back(static_cast<unsigned int>(timeSeed >> 32));
    seedValues.push_back(static_cast<unsigned int>(tickSeed));
    seedValues.push_back(static_cast<unsigned int>(tickSeed >> 32));
    seedValues.push_back(static_cast<unsigned int>(pidSeed));
    seedValues.push_back(static_cast<unsigned int>(tidSeed));
    seedValues.push_back(static_cast<unsigned int>(perfSeed));
    seedValues.push_back(static_cast<unsigned int>(perfSeed >> 32));
    seedValues.push_back(static_cast<unsigned int>(randomDeviceSeed));
    seedValues.push_back(static_cast<unsigned int>(randomDeviceSeed >> 32));

    std::seed_seq seedData(seedValues.begin(), seedValues.end());
    return std::mt19937_64(seedData);
#else
    const unsigned long long timeSeed = static_cast<unsigned long long>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const unsigned long long tickSeed = static_cast<unsigned long long>(::clock());
    const unsigned long long pidSeed = static_cast<unsigned long long>(::getpid());
    const unsigned long long tidSeed = static_cast<unsigned long long>(
        static_cast<long>(::syscall(SYS_gettid)));
    struct timespec monotonic;
    ::clock_gettime(CLOCK_MONOTONIC, &monotonic);
    const unsigned long long perfSeed = static_cast<unsigned long long>(monotonic.tv_nsec);
    const unsigned long long randomDeviceSeed =
        static_cast<unsigned long long>(std::random_device{}());

    std::vector<unsigned int> seedValues;
    seedValues.push_back(static_cast<unsigned int>(timeSeed));
    seedValues.push_back(static_cast<unsigned int>(timeSeed >> 32));
    seedValues.push_back(static_cast<unsigned int>(tickSeed));
    seedValues.push_back(static_cast<unsigned int>(pidSeed));
    seedValues.push_back(static_cast<unsigned int>(tidSeed));
    seedValues.push_back(static_cast<unsigned int>(perfSeed));
    seedValues.push_back(static_cast<unsigned int>(perfSeed >> 32));
    seedValues.push_back(static_cast<unsigned int>(randomDeviceSeed));
    seedValues.push_back(static_cast<unsigned int>(randomDeviceSeed >> 32));

    std::seed_seq seedData(seedValues.begin(), seedValues.end());
    return std::mt19937_64(seedData);
#endif
}

}  // namespace sek

// JIT helpers (declared in Vm.h). Kept here so they can touch Vm internals.

namespace sek {

void jitStoreNumber(Vm* vm, uint32_t slot, uint64_t bits) {
    if (slot >= vm->slots_.size() || !vm->defined_[slot]) {
        vm->jitResult_ = 1;
        return;
    }

    double number;
    std::memcpy(&number, &bits, sizeof(number));
    vm->slots_[slot] = Value(number);
}

void jitDefineNumber(Vm* vm, uint32_t slot, uint64_t bits) {
    if (slot >= vm->slots_.size()) {
        vm->jitResult_ = 1;
        return;
    }

    double number;
    std::memcpy(&number, &bits, sizeof(number));
    vm->slots_[slot] = Value(number);
    vm->defined_[slot] = 1;
}

double jitLoadNumber(Vm* vm, uint32_t slot) {
    if (slot >= vm->slots_.size() || !vm->defined_[slot] ||
        vm->slots_[slot].type() != Value::Type::Number) {
        vm->jitResult_ = 1;
        return 0.0;
    }

    return vm->slots_[slot].asNumber();
}

void jitPollHotkeys(Vm* vm) {
    vm->processPendingHotkeys();
    vm->jitSlotsCache_ = vm->slots_.data();
    vm->jitDefinedCache_ = vm->defined_.data();
    vm->jitSlotCountCache_ = static_cast<uint32_t>(vm->defined_.size());
}

bool jitConsumeLoopStop(Vm* vm, uint32_t id) {
    return vm->consumeLoopStop(id);
}

void jitRequestLoopStop(Vm* vm, uint32_t id) {
    vm->requestLoopStop(id);
}

void jitPrintNumber(uint64_t bits) {
    double number;
    std::memcpy(&number, &bits, sizeof(number));
    std::cout << number << '\n';
}

void jitSleep(Vm* vm, uint64_t bits) {
    double number;
    std::memcpy(&number, &bits, sizeof(number));
    if (number < 0.0) {
        vm->jitResult_ = 1;
        return;
    }

    preciseSleepMs(number);
}

bool jitIsStopRequested() {
    return isStopRequested();
}

}  // namespace sek
