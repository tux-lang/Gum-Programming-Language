#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Token.h"

namespace sek {

struct Expr {
    virtual ~Expr() = default;
};

using ExprPtr = std::unique_ptr<Expr>;

struct LiteralExpr final : Expr {
    explicit LiteralExpr(Token literalToken) : token(std::move(literalToken)) {}
    Token token;
};

struct VariableExpr final : Expr {
    explicit VariableExpr(Token variableName) : name(std::move(variableName)) {}
    Token name;
};

struct GroupingExpr final : Expr {
    explicit GroupingExpr(ExprPtr inner) : expression(std::move(inner)) {}
    ExprPtr expression;
};

struct UnaryExpr final : Expr {
    UnaryExpr(Token operatorToken, ExprPtr rightExpr)
        : op(std::move(operatorToken)), right(std::move(rightExpr)) {}

    Token op;
    ExprPtr right;
};

struct BinaryExpr final : Expr {
    BinaryExpr(ExprPtr leftExpr, Token operatorToken, ExprPtr rightExpr)
        : left(std::move(leftExpr)), op(std::move(operatorToken)), right(std::move(rightExpr)) {}

    ExprPtr left;
    Token op;
    ExprPtr right;
};

struct CallExpr final : Expr {
    CallExpr(Token calleeName, std::vector<ExprPtr> argumentsList)
        : callee(std::move(calleeName)), arguments(std::move(argumentsList)) {}

    Token callee;
    std::vector<ExprPtr> arguments;
};

}  // namespace sek
