#include "Compiler.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace sek {

namespace {

bool isBuiltinFunctionName(const std::string& name) {
    return name == "input" || name == "random" || name == "rand" || name == "seed" ||
           name == "type" || name == "assert" || name == "exists" ||
           name == "isfile" || name == "isdir" || name == "touch" ||
           name == "write" || name == "append" || name == "read" ||
           name == "remove" || name == "mkdir" || name == "rmdir" ||
           name == "rename" || name == "copy" || name == "os_exists" ||
           name == "os_isfile" || name == "os_isdir" || name == "os_touch" ||
           name == "os_write" || name == "os_append" || name == "os_read" ||
           name == "os_remove" || name == "os_mkdir" || name == "os_rmdir" ||
           name == "os_rename" || name == "os_copy" || name == "http_get" ||
           name == "http_post" || name == "json_parse" || name == "json_get" ||
           name == "json_array_length" || name == "json_array_get" ||
           name == "json_type" || name == "tg_cmd_hash";
}

BuiltinId builtinIdForName(const std::string& name) {
    if (name == "input") return B_INPUT;
    if (name == "random" || name == "rand") return B_RANDOM;
    if (name == "seed") return B_SEED;
    if (name == "type") return B_TYPE;
    if (name == "assert") return B_ASSERT;
    if (name == "exists" || name == "os_exists") return B_EXISTS;
    if (name == "isfile" || name == "os_isfile") return B_ISFILE;
    if (name == "isdir" || name == "os_isdir") return B_ISDIR;
    if (name == "touch" || name == "os_touch") return B_TOUCH;
    if (name == "write" || name == "os_write") return B_WRITE;
    if (name == "append" || name == "os_append") return B_APPEND;
    if (name == "read" || name == "os_read") return B_READ;
    if (name == "remove" || name == "os_remove") return B_REMOVE;
    if (name == "mkdir" || name == "os_mkdir") return B_MKDIR;
    if (name == "rmdir" || name == "os_rmdir") return B_RMDIR;
    if (name == "rename" || name == "os_rename") return B_RENAME;
    if (name == "copy" || name == "os_copy") return B_COPY;
    if (name == "http_get") return B_HTTP_GET;
    if (name == "http_post") return B_HTTP_POST;
    if (name == "json_parse") return B_JSON_PARSE;
    if (name == "json_get") return B_JSON_GET;
    if (name == "json_array_length") return B_JSON_ARRAY_LENGTH;
    if (name == "json_array_get") return B_JSON_ARRAY_GET;
    if (name == "json_type") return B_JSON_TYPE;
    if (name == "tg_cmd_hash") return B_TG_CMD_HASH;
    return B_COUNT;
}

OpCode binaryOpcodeFor(TokenType type) {
    switch (type) {
        case TokenType::Plus:
            return OP_ADD;
        case TokenType::Minus:
            return OP_SUB;
        case TokenType::Star:
            return OP_MUL;
        case TokenType::Slash:
            return OP_DIV;
        case TokenType::Greater:
            return OP_GREATER;
        case TokenType::GreaterEqual:
            return OP_GREATER_EQUAL;
        case TokenType::Less:
            return OP_LESS;
        case TokenType::LessEqual:
            return OP_LESS_EQUAL;
        case TokenType::EqualEqual:
            return OP_EQUAL;
        case TokenType::BangEqual:
            return OP_NOT_EQUAL;
        default:
            break;
    }

    return OP_COUNT;
}

TgCommandId tgCommandIdForName(const std::string& name) {
    if (name == "tg_token") return TG_TOKEN;
    if (name == "tg_on") return TG_ON;
    if (name == "tg_send") return TG_SEND;
    if (name == "tg_reply") return TG_REPLY;
    if (name == "tg_typing") return TG_TYPING;
    if (name == "tg_photo") return TG_PHOTO;
    if (name == "tg_sticker") return TG_STICKER;
    if (name == "tg_delete") return TG_DELETE;
    if (name == "tg_edit") return TG_EDIT;
    if (name == "tg_callback_answer") return TG_CALLBACK_ANSWER;
    if (name == "tg_get_chat") return TG_GET_CHAT;
    if (name == "tg_leave") return TG_LEAVE;
    return TG_COUNT;
}

}  // namespace

Module Compiler::compile(const Program& program) {
    Compiler compiler;
    compiler.collectNames(program);
    compiler.compileFunction(program, "", true);
    return std::move(compiler.module_);
}

uint32_t Compiler::addString(const std::string& text) {
    module_.strings.push_back(text);
    return static_cast<uint32_t>(module_.strings.size() - 1);
}

uint32_t Compiler::addNumber(double number) {
    module_.numbers.push_back(number);
    return static_cast<uint32_t>(module_.numbers.size() - 1);
}

uint32_t Compiler::resolveSlot(const std::string& name) {
    const std::unordered_map<std::string, uint32_t>::const_iterator found =
        module_.nameToSlot.find(name);
    if (found != module_.nameToSlot.end()) {
        return found->second;
    }

    const uint32_t slot = static_cast<uint32_t>(module_.slotNames.size());
    module_.nameToSlot[name] = slot;
    module_.slotNames.push_back(name);
    return slot;
}

uint32_t Compiler::emit(uint32_t word) {
    code_.push_back(word);
    return static_cast<uint32_t>(code_.size() - 1);
}

void Compiler::patch(uint32_t at, uint32_t value) {
    code_[at] = value;
}

void Compiler::collectNames(const Program& program) {
    for (const auto& statement : program) {
        if (const auto* print = dynamic_cast<const PrintStmt*>(statement.get())) {
            collectExprNames(*print->expression);
        } else if (const auto* msg = dynamic_cast<const MsgStmt*>(statement.get())) {
            collectExprNames(*msg->expression);
        } else if (const auto* playSound = dynamic_cast<const PlaySoundStmt*>(statement.get())) {
            collectExprNames(*playSound->path);
        } else if (const auto* send = dynamic_cast<const SendStmt*>(statement.get())) {
            for (std::vector<SendPart>::const_iterator it = send->parts.begin();
                 it != send->parts.end(); ++it) {
                if (it->type == SendPart::Type::Expression) {
                    collectExprNames(*it->expression);
                }
            }
        } else if (const auto* save = dynamic_cast<const SaveStmt*>(statement.get())) {
            collectExprNames(*save->path);
        } else if (const auto* load = dynamic_cast<const LoadStmt*>(statement.get())) {
            collectExprNames(*load->path);
        } else if (const auto* mouseMove = dynamic_cast<const MouseMoveStmt*>(statement.get())) {
            collectExprNames(*mouseMove->x);
            collectExprNames(*mouseMove->y);
        } else if (const auto* mouseHold = dynamic_cast<const MouseHoldStmt*>(statement.get())) {
            collectExprNames(*mouseHold->duration);
        } else if (const auto* sleep = dynamic_cast<const SleepStmt*>(statement.get())) {
            collectExprNames(*sleep->duration);
        } else if (const auto* toggle = dynamic_cast<const ToggleStmt*>(statement.get())) {
            resolveSlot(toggle->name.lexeme);
        } else if (const auto* loop = dynamic_cast<const LoopStmt*>(statement.get())) {
            if (!loop->infinite && loop->count) {
                collectExprNames(*loop->count);
            }
            collectNames(loop->body);
        } else if (const auto* ifStatement = dynamic_cast<const IfStmt*>(statement.get())) {
            collectExprNames(*ifStatement->condition);
            collectNames(ifStatement->thenBody);
            for (std::vector<ConditionalBranch>::const_iterator it = ifStatement->elifs.begin();
                 it != ifStatement->elifs.end(); ++it) {
                collectExprNames(*it->condition);
                collectNames(it->body);
            }
            collectNames(ifStatement->elseBody);
        } else if (const auto* let = dynamic_cast<const LetStmt*>(statement.get())) {
            resolveSlot(let->name.lexeme);
            collectExprNames(*let->initializer);
        } else if (const auto* assign = dynamic_cast<const AssignStmt*>(statement.get())) {
            resolveSlot(assign->name.lexeme);
            collectExprNames(*assign->value);
        } else if (const auto* expression = dynamic_cast<const ExpressionStmt*>(statement.get())) {
            collectExprNames(*expression->expression);
        } else if (const auto* tg = dynamic_cast<const TgStmt*>(statement.get())) {
            for (std::vector<ExprPtr>::const_iterator it = tg->args.begin();
                 it != tg->args.end(); ++it) {
                collectExprNames(**it);
            }
        } else if (const auto* group = dynamic_cast<const GroupStmt*>(statement.get())) {
            collectNames(group->body);
        } else if (const auto* hotkey = dynamic_cast<const HotkeyStmt*>(statement.get())) {
            collectNames(hotkey->body);
        }
    }
}

void Compiler::collectExprNames(const Expr& expression) {
    if (const auto* variable = dynamic_cast<const VariableExpr*>(&expression)) {
        resolveSlot(variable->name.lexeme);
    } else if (const auto* grouping = dynamic_cast<const GroupingExpr*>(&expression)) {
        collectExprNames(*grouping->expression);
    } else if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expression)) {
        collectExprNames(*unary->right);
    } else if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expression)) {
        collectExprNames(*binary->left);
        collectExprNames(*binary->right);
    } else if (const auto* call = dynamic_cast<const CallExpr*>(&expression)) {
        for (std::vector<ExprPtr>::const_iterator it = call->arguments.begin();
             it != call->arguments.end(); ++it) {
            collectExprNames(**it);
        }
    }
}

uint32_t Compiler::compileFunction(const Program& body, const std::string& name, bool topLevel) {
    const uint32_t funcIndex = static_cast<uint32_t>(module_.functions.size());
    std::vector<uint32_t> savedCode = std::move(code_);
    code_.clear();

    module_.functions.push_back(Function{name, {}});
    compileStatements(body, topLevel);
    module_.functions[funcIndex].code = std::move(code_);

    code_ = std::move(savedCode);
    return funcIndex;
}

void Compiler::compileStatements(const Program& statements, bool topLevel) {
    for (const auto& statement : statements) {
        compileStatement(*statement, topLevel);
    }
}

void Compiler::compileStatement(const Stmt& statement, bool topLevel) {
    if (const auto* group = dynamic_cast<const GroupStmt*>(&statement)) {
        const std::string groupName = group->name.lexeme;
        const uint32_t funcIndex = compileFunction(group->body, groupName, false);
        module_.groupMap[groupName] = funcIndex;
        return;
    }

    if (const auto* hotkey = dynamic_cast<const HotkeyStmt*>(&statement)) {
        if (topLevel) {
            const std::string trigger = hotkey->trigger.lexeme;
            const uint32_t funcIndex = compileFunction(hotkey->body, trigger, false);
            module_.hotkeys.push_back(std::make_pair(trigger, funcIndex));
        }
        return;
    }

    if (const auto* print = dynamic_cast<const PrintStmt*>(&statement)) {
        compileExpr(*print->expression);
        emit(OP_PRINT);
        return;
    }

    if (const auto* msg = dynamic_cast<const MsgStmt*>(&statement)) {
        compileExpr(*msg->expression);
        emit(OP_MSG);
        return;
    }

    if (const auto* playSound = dynamic_cast<const PlaySoundStmt*>(&statement)) {
        compileExpr(*playSound->path);
        emit(OP_PLAY_SOUND);
        return;
    }

    if (const auto* send = dynamic_cast<const SendStmt*>(&statement)) {
        for (std::vector<SendPart>::const_iterator it = send->parts.begin();
             it != send->parts.end(); ++it) {
            if (it->type == SendPart::Type::Expression) {
                compileExpr(*it->expression);
                emit(OP_SEND_TEXT);
            } else {
                emit(OP_SEND_KEY);
                emit(addString(it->key));
            }
        }
        return;
    }

    if (const auto* save = dynamic_cast<const SaveStmt*>(&statement)) {
        compileExpr(*save->path);
        emit(OP_SAVE);
        return;
    }

    if (const auto* load = dynamic_cast<const LoadStmt*>(&statement)) {
        compileExpr(*load->path);
        emit(OP_LOAD_FILE);
        return;
    }

    if (dynamic_cast<const ClickStmt*>(&statement) != nullptr) {
        emit(OP_CLICK);
        return;
    }

    if (dynamic_cast<const MouseDownStmt*>(&statement) != nullptr) {
        emit(OP_MOUSE_DOWN);
        return;
    }

    if (dynamic_cast<const MouseUpStmt*>(&statement) != nullptr) {
        emit(OP_MOUSE_UP);
        return;
    }

    if (const auto* mouseMove = dynamic_cast<const MouseMoveStmt*>(&statement)) {
        compileExpr(*mouseMove->x);
        compileExpr(*mouseMove->y);
        emit(OP_MOUSE_MOVE);
        return;
    }

    if (const auto* mouseHold = dynamic_cast<const MouseHoldStmt*>(&statement)) {
        compileExpr(*mouseHold->duration);
        emit(OP_MOUSE_HOLD);
        return;
    }

    if (const auto* sleep = dynamic_cast<const SleepStmt*>(&statement)) {
        compileExpr(*sleep->duration);
        emit(OP_SLEEP);
        return;
    }

    if (const auto* toggle = dynamic_cast<const ToggleStmt*>(&statement)) {
        emit(OP_TOGGLE);
        emit(resolveSlot(toggle->name.lexeme));
        return;
    }

    if (const auto* unloop = dynamic_cast<const UnloopStmt*>(&statement)) {
        emit(OP_UNLOOP);
        emit(static_cast<uint32_t>(unloop->id));
        return;
    }

    if (const auto* loop = dynamic_cast<const LoopStmt*>(&statement)) {
        if (!loop->infinite && loop->count) {
            compileExpr(*loop->count);
        }

        emit(OP_LOOP);
        emit(loop->infinite ? 1u : 0u);
        emit(static_cast<uint32_t>(loop->id));
        const uint32_t bodyStartPatch = emit(0);
        const uint32_t afterLoopPatch = emit(0);
        const uint32_t bodyStart = here();
        patch(bodyStartPatch, bodyStart);

        compileStatements(loop->body, false);

        emit(OP_LOOP_BACK);
        emit(bodyStart);
        const uint32_t afterBackPatch = emit(0);
        const uint32_t afterLoop = here();
        patch(afterBackPatch, afterLoop);
        patch(afterLoopPatch, afterLoop);
        return;
    }

    if (const auto* ifStatement = dynamic_cast<const IfStmt*>(&statement)) {
        std::vector<uint32_t> jumpsToEnd;

        compileExpr(*ifStatement->condition);
        emit(OP_JUMP_IF_FALSE);
        const uint32_t ifEndPatch = emit(0);

        compileStatements(ifStatement->thenBody, topLevel);
        emit(OP_JUMP);
        jumpsToEnd.push_back(emit(0));

        patch(ifEndPatch, here());

        for (std::vector<ConditionalBranch>::const_iterator it = ifStatement->elifs.begin();
             it != ifStatement->elifs.end(); ++it) {
            compileExpr(*it->condition);
            emit(OP_JUMP_IF_FALSE);
            const uint32_t elifEndPatch = emit(0);

            compileStatements(it->body, topLevel);
            emit(OP_JUMP);
            jumpsToEnd.push_back(emit(0));

            patch(elifEndPatch, here());
        }

        compileStatements(ifStatement->elseBody, topLevel);

        for (std::vector<uint32_t>::const_iterator it = jumpsToEnd.begin();
             it != jumpsToEnd.end(); ++it) {
            patch(*it, here());
        }
        return;
    }

    if (const auto* let = dynamic_cast<const LetStmt*>(&statement)) {
        const uint32_t slot = resolveSlot(let->name.lexeme);
        compileExpr(*let->initializer);
        emit(OP_DEFINE);
        emit(slot);
        return;
    }

    if (const auto* assign = dynamic_cast<const AssignStmt*>(&statement)) {
        const uint32_t slot = resolveSlot(assign->name.lexeme);
        compileExpr(*assign->value);
        emit(OP_STORE);
        emit(slot);
        return;
    }

    if (const auto* expression = dynamic_cast<const ExpressionStmt*>(&statement)) {
        compileExpr(*expression->expression);
        emit(OP_POP);
        return;
    }

    if (const auto* tg = dynamic_cast<const TgStmt*>(&statement)) {
        for (std::vector<ExprPtr>::const_iterator it = tg->args.begin();
             it != tg->args.end(); ++it) {
            compileExpr(**it);
        }

        const TgCommandId commandId = tgCommandIdForName(tg->name.lexeme);
        emit(OP_TG_CALL);
        emit(static_cast<uint32_t>(commandId));
        emit(static_cast<uint32_t>(tg->args.size()));
        return;
    }
}

void Compiler::compileExpr(const Expr& expression) {
    if (const auto* literal = dynamic_cast<const LiteralExpr*>(&expression)) {
        switch (literal->token.type) {
            case TokenType::Number:
                emit(OP_CONST_NUM);
                emit(addNumber(std::stod(literal->token.lexeme)));
                return;
            case TokenType::String:
                emit(OP_CONST_STR);
                emit(addString(literal->token.lexeme));
                return;
            case TokenType::True:
                emit(OP_CONST_TRUE);
                return;
            case TokenType::False:
                emit(OP_CONST_FALSE);
                return;
            default:
                break;
        }
    }

    if (const auto* variable = dynamic_cast<const VariableExpr*>(&expression)) {
        emit(OP_LOAD);
        emit(resolveSlot(variable->name.lexeme));
        return;
    }

    if (const auto* grouping = dynamic_cast<const GroupingExpr*>(&expression)) {
        compileExpr(*grouping->expression);
        return;
    }

    if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expression)) {
        compileExpr(*unary->right);
        if (unary->op.type == TokenType::Minus) {
            emit(OP_NEGATE);
        } else {
            emit(OP_NOT);
        }
        return;
    }

    if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expression)) {
        if (binary->op.type == TokenType::And) {
            compileExpr(*binary->left);
            emit(OP_JUMP_IF_FALSE);
            const uint32_t falsePatch = emit(0);
            compileExpr(*binary->right);
            emit(OP_JUMP_IF_FALSE);
            const uint32_t falsePatch2 = emit(0);
            emit(OP_CONST_TRUE);
            emit(OP_JUMP);
            const uint32_t endPatch = emit(0);
            patch(falsePatch, here());
            patch(falsePatch2, here());
            emit(OP_CONST_FALSE);
            patch(endPatch, here());
            return;
        }

        if (binary->op.type == TokenType::Or) {
            compileExpr(*binary->left);
            emit(OP_JUMP_IF_TRUE);
            const uint32_t truePatch = emit(0);
            compileExpr(*binary->right);
            emit(OP_JUMP_IF_TRUE);
            const uint32_t truePatch2 = emit(0);
            emit(OP_CONST_FALSE);
            emit(OP_JUMP);
            const uint32_t endPatch = emit(0);
            patch(truePatch, here());
            patch(truePatch2, here());
            emit(OP_CONST_TRUE);
            patch(endPatch, here());
            return;
        }

        compileExpr(*binary->left);
        compileExpr(*binary->right);
        const OpCode opcode = binaryOpcodeFor(binary->op.type);
        if (opcode != OP_COUNT) {
            emit(opcode);
        }
        return;
    }

    if (const auto* call = dynamic_cast<const CallExpr*>(&expression)) {
        const std::string name = call->callee.lexeme;
        for (std::vector<ExprPtr>::const_iterator it = call->arguments.begin();
             it != call->arguments.end(); ++it) {
            compileExpr(**it);
        }

        if (isBuiltinFunctionName(name)) {
            emit(OP_CALL_BUILTIN);
            emit(static_cast<uint32_t>(builtinIdForName(name)));
            emit(static_cast<uint32_t>(call->arguments.size()));
        } else {
            emit(OP_CALL_GROUP);
            emit(addString(name));
            emit(static_cast<uint32_t>(call->arguments.size()));
        }
        return;
    }
}

}  // namespace sek
