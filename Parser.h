#pragma once

#include <cstddef>
#include <vector>

#include "Stmt.h"
#include "Token.h"

namespace sek {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    Program parse();

private:
    bool isAtEnd() const;
    const Token& peek() const;
    const Token& previous() const;
    bool check(TokenType type) const;
    bool checkNext(TokenType type) const;
    bool checkAt(std::size_t offset, TokenType type) const;
    bool isHotkeyHeader() const;
    bool isInlineHotkeyHeader() const;
    bool match(std::initializer_list<TokenType> types);
    const Token& advance();
    const Token& consume(TokenType type, const std::string& message);
    void skipSeparators();
    void consumeStatementTerminator();
    bool isStatementTerminator() const;
    bool startsExpression() const;
    int consumeLoopId();
    Program parseBlockUntil(std::initializer_list<TokenType> stopTokens);

    StmtPtr declaration();
    StmtPtr hotkeyDeclaration();
    StmtPtr inlineHotkeyDeclaration();
    StmtPtr letDeclaration();
    StmtPtr useDeclaration();
    StmtPtr groupDeclaration();
    StmtPtr statement();
    StmtPtr setStatement();
    StmtPtr printStatement();
    StmtPtr msgStatement();
    StmtPtr playSoundStatement();
    StmtPtr sendStatement();
    StmtPtr saveStatement();
    StmtPtr loadStatement();
    StmtPtr clickStatement();
    StmtPtr mouseDownStatement();
    StmtPtr mouseUpStatement();
    StmtPtr mouseMoveStatement();
    StmtPtr mouseHoldStatement();
    StmtPtr sleepStatement();
    StmtPtr toggleStatement();
    StmtPtr unloopStatement();
    StmtPtr loopStatement();
    StmtPtr ifStatement();
    StmtPtr assignmentStatement();
    StmtPtr expressionStatement();
    StmtPtr tgStatement();

    ExprPtr expression();
    ExprPtr orExpression();
    ExprPtr andExpression();
    ExprPtr equality();
    ExprPtr comparison();
    ExprPtr term();
    ExprPtr factor();
    ExprPtr unary();
    ExprPtr call();
    ExprPtr primary();

    std::vector<Token> tokens_;
    std::size_t current_ = 0;
};

}  // namespace sek
