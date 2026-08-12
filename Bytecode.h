#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sek {

enum OpCode : uint32_t {
    OP_CONST_NUM = 0,
    OP_CONST_STR,
    OP_CONST_TRUE,
    OP_CONST_FALSE,
    OP_CONST_NIL,
    OP_LOAD,
    OP_STORE,
    OP_DEFINE,
    OP_TOGGLE,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_NEGATE,
    OP_NOT,
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_GREATER,
    OP_GREATER_EQUAL,
    OP_LESS,
    OP_LESS_EQUAL,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_JUMP_IF_TRUE,
    OP_POP,
    OP_PRINT,
    OP_MSG,
    OP_PLAY_SOUND,
    OP_SEND_TEXT,
    OP_SEND_KEY,
    OP_SAVE,
    OP_LOAD_FILE,
    OP_CLICK,
    OP_MOUSE_DOWN,
    OP_MOUSE_UP,
    OP_MOUSE_MOVE,
    OP_MOUSE_HOLD,
    OP_SLEEP,
    OP_UNLOOP,
    OP_LOOP,
    OP_LOOP_BACK,
    OP_CALL_BUILTIN,
    OP_CALL_GROUP,
    OP_TG_CALL,
    OP_COUNT
};

enum TgCommandId : uint32_t {
    TG_TOKEN = 0,
    TG_ON,
    TG_SEND,
    TG_REPLY,
    TG_TYPING,
    TG_PHOTO,
    TG_STICKER,
    TG_DELETE,
    TG_EDIT,
    TG_CALLBACK_ANSWER,
    TG_GET_CHAT,
    TG_LEAVE,
    TG_COUNT
};

enum BuiltinId : uint32_t {
    B_INPUT = 0,
    B_RANDOM,
    B_SEED,
    B_TYPE,
    B_ASSERT,
    B_EXISTS,
    B_ISFILE,
    B_ISDIR,
    B_TOUCH,
    B_WRITE,
    B_APPEND,
    B_READ,
    B_REMOVE,
    B_MKDIR,
    B_RMDIR,
    B_RENAME,
    B_COPY,
    B_HTTP_GET,
    B_HTTP_POST,
    B_JSON_PARSE,
    B_JSON_GET,
    B_JSON_ARRAY_LENGTH,
    B_JSON_ARRAY_GET,
    B_JSON_TYPE,
    B_TG_CMD_HASH,
    B_COUNT
};

constexpr uint32_t kOpcodeWordCount[OP_COUNT] = {
    2,  // OP_CONST_NUM
    2,  // OP_CONST_STR
    1,  // OP_CONST_TRUE
    1,  // OP_CONST_FALSE
    1,  // OP_CONST_NIL
    2,  // OP_LOAD
    2,  // OP_STORE
    2,  // OP_DEFINE
    2,  // OP_TOGGLE
    1,  // OP_ADD
    1,  // OP_SUB
    1,  // OP_MUL
    1,  // OP_DIV
    1,  // OP_NEGATE
    1,  // OP_NOT
    1,  // OP_EQUAL
    1,  // OP_NOT_EQUAL
    1,  // OP_GREATER
    1,  // OP_GREATER_EQUAL
    1,  // OP_LESS
    1,  // OP_LESS_EQUAL
    2,  // OP_JUMP
    2,  // OP_JUMP_IF_FALSE
    2,  // OP_JUMP_IF_TRUE
    1,  // OP_POP
    1,  // OP_PRINT
    1,  // OP_MSG
    1,  // OP_PLAY_SOUND
    1,  // OP_SEND_TEXT
    2,  // OP_SEND_KEY
    1,  // OP_SAVE
    1,  // OP_LOAD_FILE
    1,  // OP_CLICK
    1,  // OP_MOUSE_DOWN
    1,  // OP_MOUSE_UP
    1,  // OP_MOUSE_MOVE
    1,  // OP_MOUSE_HOLD
    1,  // OP_SLEEP
    2,  // OP_UNLOOP
    5,  // OP_LOOP: flag, id, bodyStart, afterLoop
    3,  // OP_LOOP_BACK: bodyStart, afterLoop
    3,  // OP_CALL_BUILTIN: builtinId, argCount
    3,  // OP_CALL_GROUP: stringIdx, argCount
    3   // OP_TG_CALL: tgCommandId, argCount
};

struct Function {
    std::string name;
    std::vector<uint32_t> code;
};

struct Module {
    std::vector<double> numbers;
    std::vector<std::string> strings;
    std::vector<Function> functions;                        // [0] = top level
    std::unordered_map<std::string, uint32_t> nameToSlot;   // variable name -> slot
    std::vector<std::string> slotNames;
    std::unordered_map<std::string, uint32_t> groupMap;     // group name -> function index
    std::vector<std::pair<std::string, uint32_t>> hotkeys;  // trigger -> function index
};

}  // namespace sek
