#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "Expr.h"
#include "Token.h"

namespace sek {

struct Stmt {
    virtual ~Stmt() = default;
};

using StmtPtr = std::unique_ptr<Stmt>;
using Program = std::vector<StmtPtr>;

struct SendPart {
    enum class Type {
        Expression,
        Key
    };

    explicit SendPart(ExprPtr expr) : type(Type::Expression), expression(std::move(expr)) {}
    explicit SendPart(std::string keyName) : type(Type::Key), key(std::move(keyName)) {}

    SendPart(SendPart&& other) noexcept
        : type(other.type), expression(std::move(other.expression)), key(std::move(other.key)) {}

    SendPart& operator=(SendPart&& other) noexcept {
        if (this != &other) {
            type = other.type;
            expression = std::move(other.expression);
            key = std::move(other.key);
        }

        return *this;
    }

    SendPart(const SendPart&) = delete;
    SendPart& operator=(const SendPart&) = delete;

    Type type;
    ExprPtr expression;
    std::string key;
};

struct PrintStmt final : Stmt {
    explicit PrintStmt(ExprPtr expr) : expression(std::move(expr)) {}
    ExprPtr expression;
};

struct MsgStmt final : Stmt {
    explicit MsgStmt(ExprPtr expr) : expression(std::move(expr)) {}
    ExprPtr expression;
};

struct PlaySoundStmt final : Stmt {
    explicit PlaySoundStmt(ExprPtr expr) : path(std::move(expr)) {}
    ExprPtr path;
};

struct SendStmt final : Stmt {
    explicit SendStmt(std::vector<SendPart> sendParts) : parts(std::move(sendParts)) {}
    std::vector<SendPart> parts;
};

struct SaveStmt final : Stmt {
    explicit SaveStmt(ExprPtr expr) : path(std::move(expr)) {}
    ExprPtr path;
};

struct LoadStmt final : Stmt {
    explicit LoadStmt(ExprPtr expr) : path(std::move(expr)) {}
    ExprPtr path;
};

struct UseStmt final : Stmt {
    explicit UseStmt(Token pathToken) : path(std::move(pathToken)) {}
    Token path;
};

struct GroupStmt final : Stmt {
    GroupStmt(Token functionName, Program bodyStatements)
        : name(std::move(functionName)), body(std::move(bodyStatements)) {}

    Token name;
    Program body;
};

struct ClickStmt final : Stmt {
};

struct MouseDownStmt final : Stmt {
};

struct MouseUpStmt final : Stmt {
};

struct MouseMoveStmt final : Stmt {
    MouseMoveStmt(ExprPtr xExpr, ExprPtr yExpr)
        : x(std::move(xExpr)), y(std::move(yExpr)) {}

    ExprPtr x;
    ExprPtr y;
};

struct MouseHoldStmt final : Stmt {
    explicit MouseHoldStmt(ExprPtr expr) : duration(std::move(expr)) {}
    ExprPtr duration;
};

struct SleepStmt final : Stmt {
    explicit SleepStmt(ExprPtr expr) : duration(std::move(expr)) {}
    ExprPtr duration;
};

struct ToggleStmt final : Stmt {
    explicit ToggleStmt(Token variableName) : name(std::move(variableName)) {}
    Token name;
};

struct UnloopStmt final : Stmt {
    explicit UnloopStmt(int loopNumber) : id(loopNumber) {}
    int id = 0;
};

struct LoopStmt final : Stmt {
    LoopStmt(ExprPtr repeatCount, Program bodyStatements, bool infiniteLoop = false, int loopNumber = 0)
        : count(std::move(repeatCount)),
          body(std::move(bodyStatements)),
          infinite(infiniteLoop),
          id(loopNumber) {}

    ExprPtr count;
    Program body;
    bool infinite = false;
    int id = 0;
};

struct ConditionalBranch {
    ConditionalBranch(ExprPtr conditionExpr, Program bodyStatements)
        : condition(std::move(conditionExpr)), body(std::move(bodyStatements)) {}

    ConditionalBranch(ConditionalBranch&& other) noexcept
        : condition(std::move(other.condition)), body(std::move(other.body)) {}

    ConditionalBranch& operator=(ConditionalBranch&& other) noexcept {
        if (this != &other) {
            condition = std::move(other.condition);
            body = std::move(other.body);
        }

        return *this;
    }

    ConditionalBranch(const ConditionalBranch&) = delete;
    ConditionalBranch& operator=(const ConditionalBranch&) = delete;

    ExprPtr condition;
    Program body;
};

struct IfStmt final : Stmt {
    IfStmt(ExprPtr ifCondition, Program ifBody, std::vector<ConditionalBranch> elifBranches,
           Program elseBodyStatements)
        : condition(std::move(ifCondition)),
          thenBody(std::move(ifBody)),
          elifs(std::move(elifBranches)),
          elseBody(std::move(elseBodyStatements)) {}

    ExprPtr condition;
    Program thenBody;
    std::vector<ConditionalBranch> elifs;
    Program elseBody;
};

struct LetStmt final : Stmt {
    LetStmt(Token variableName, ExprPtr initializerExpr)
        : name(std::move(variableName)), initializer(std::move(initializerExpr)) {}

    Token name;
    ExprPtr initializer;
};

struct AssignStmt final : Stmt {
    AssignStmt(Token variableName, ExprPtr valueExpr)
        : name(std::move(variableName)), value(std::move(valueExpr)) {}

    Token name;
    ExprPtr value;
};

struct ExpressionStmt final : Stmt {
    explicit ExpressionStmt(ExprPtr expr) : expression(std::move(expr)) {}
    ExprPtr expression;
};

struct TgStmt final : Stmt {
    TgStmt(Token commandName, std::vector<ExprPtr> argExprs)
        : name(std::move(commandName)), args(std::move(argExprs)) {}

    Token name;
    std::vector<ExprPtr> args;
};

struct HotkeyStmt final : Stmt {
    HotkeyStmt(Token triggerToken, Program bodyStatements)
        : trigger(std::move(triggerToken)), body(std::move(bodyStatements)) {}

    Token trigger;
    Program body;
};

}  // namespace sek
