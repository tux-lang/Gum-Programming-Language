#include "Parser.h"

#include <cmath>
#include <initializer_list>

#include "Error.h"

namespace sek {

namespace {

// Word-style Telegram commands (args are space-separated expressions).
bool isTgCommandName(const std::string& name) {
    return name == "tg_token" || name == "tg_on" || name == "tg_send" ||
           name == "tg_reply" || name == "tg_typing" || name == "tg_photo" ||
           name == "tg_sticker" || name == "tg_delete" || name == "tg_edit" ||
           name == "tg_callback_answer" || name == "tg_get_chat" || name == "tg_leave";
}

}  // namespace

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

Program Parser::parse() {
    Program program;
    skipSeparators();

    while (!isAtEnd()) {
        program.push_back(declaration());
        skipSeparators();
    }

    return program;
}

bool Parser::isAtEnd() const {
    return peek().type == TokenType::EndOfFile;
}

const Token& Parser::peek() const {
    return tokens_[current_];
}

const Token& Parser::previous() const {
    return tokens_[current_ - 1];
}

bool Parser::check(const TokenType type) const {
    if (type == TokenType::EndOfFile) {
        return peek().type == TokenType::EndOfFile;
    }

    if (isAtEnd()) {
        return false;
    }

    return peek().type == type;
}

bool Parser::checkNext(const TokenType type) const {
    return checkAt(1, type);
}

bool Parser::checkAt(const std::size_t offset, const TokenType type) const {
    if (current_ + offset >= tokens_.size()) {
        return false;
    }

    return tokens_[current_ + offset].type == type;
}

bool Parser::isHotkeyHeader() const {
    return check(TokenType::Identifier) && checkAt(1, TokenType::Colon) &&
           checkAt(2, TokenType::Colon);
}

bool Parser::isInlineHotkeyHeader() const {
    return check(TokenType::Identifier) && checkAt(1, TokenType::Colon) &&
           !checkAt(2, TokenType::Colon);
}

bool Parser::match(const std::initializer_list<TokenType> types) {
    for (const auto type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }

    return false;
}

const Token& Parser::advance() {
    if (!isAtEnd()) {
        ++current_;
    }
    return previous();
}

const Token& Parser::consume(const TokenType type, const std::string& message) {
    if (check(type)) {
        return advance();
    }

    throw ParseError(message + " in " + peek().filename + " on line " + std::to_string(peek().line));
}

void Parser::skipSeparators() {
    while (match({TokenType::NewLine, TokenType::Semicolon})) {
    }
}

void Parser::consumeStatementTerminator() {
    if (match({TokenType::Semicolon, TokenType::NewLine})) {
        skipSeparators();
        return;
    }

    if (check(TokenType::EndOfFile)) {
        return;
    }

    throw ParseError("Expected end of statement in " + peek().filename + " on line " + std::to_string(peek().line));
}

bool Parser::isStatementTerminator() const {
    return check(TokenType::Semicolon) || check(TokenType::NewLine) || check(TokenType::EndOfFile);
}

bool Parser::startsExpression() const {
    return check(TokenType::False) || check(TokenType::True) || check(TokenType::Number) ||
           check(TokenType::String) || check(TokenType::Identifier) || check(TokenType::LeftParen) ||
           check(TokenType::Bang) || check(TokenType::Minus);
}

int Parser::consumeLoopId() {
    consume(TokenType::LeftBrace, "Expected '{' before loop number");
    const Token numberToken = consume(TokenType::Number, "Expected loop number inside '{...}'");
    consume(TokenType::RightBrace, "Expected '}' after loop number");

    const double loopIdValue = std::stod(numberToken.lexeme);
    if (loopIdValue <= 0.0 || std::floor(loopIdValue) != loopIdValue) {
        throw ParseError("Loop number must be a positive integer in " + numberToken.filename + " on line " +
                 std::to_string(numberToken.line));
    }

    return static_cast<int>(loopIdValue);
}

Program Parser::parseBlockUntil(const std::initializer_list<TokenType> stopTokens) {
    Program body;
    skipSeparators();

    while (!isAtEnd()) {
        bool shouldStop = false;
        for (const auto stopToken : stopTokens) {
            if (check(stopToken)) {
                shouldStop = true;
                break;
            }
        }

        if (shouldStop) {
            break;
        }

        body.push_back(declaration());
        skipSeparators();
    }

    return body;
}

StmtPtr Parser::declaration() {
    if (isHotkeyHeader()) {
        return hotkeyDeclaration();
    }

    if (isInlineHotkeyHeader()) {
        return inlineHotkeyDeclaration();
    }

    if (match({TokenType::Let})) {
        return letDeclaration();
    }

    if (match({TokenType::Use})) {
        return useDeclaration();
    }

    if (match({TokenType::Group})) {
        return groupDeclaration();
    }

    return statement();
}

StmtPtr Parser::hotkeyDeclaration() {
    const Token trigger = consume(TokenType::Identifier, "Expected hotkey name");
    consume(TokenType::Colon, "Expected ':' in hotkey declaration");
    consume(TokenType::Colon, "Expected second ':' in hotkey declaration");

    if (match({TokenType::NewLine, TokenType::Semicolon})) {
        skipSeparators();
    }

    Program body;
    while (!isAtEnd() && !isHotkeyHeader() && !check(TokenType::End)) {
        body.push_back(declaration());
        skipSeparators();
    }

    if (body.empty()) {
        throw ParseError("Hotkey '" + trigger.lexeme + "' must contain at least one statement in " +
                         trigger.filename + " on line " + std::to_string(trigger.line));
    }

    if (match({TokenType::End})) {
        consumeStatementTerminator();
    }

    return std::make_unique<HotkeyStmt>(trigger, std::move(body));
}

StmtPtr Parser::inlineHotkeyDeclaration() {
    const Token trigger = consume(TokenType::Identifier, "Expected hotkey name");
    consume(TokenType::Colon, "Expected ':' in inline hotkey declaration");

    Program body;
    body.push_back(statement());
    return std::make_unique<HotkeyStmt>(trigger, std::move(body));
}

StmtPtr Parser::letDeclaration() {
    const Token name = consume(TokenType::Identifier, "Expected variable name after 'let'");
    consume(TokenType::Equal, "Expected '=' after variable name");
    auto initializer = expression();
    consumeStatementTerminator();
    return std::make_unique<LetStmt>(name, std::move(initializer));
}

StmtPtr Parser::useDeclaration() {
    const Token path = consume(TokenType::String, "Expected string path after 'use'");
    consumeStatementTerminator();
    return std::make_unique<UseStmt>(path);
}

StmtPtr Parser::groupDeclaration() {
    const Token name = consume(TokenType::Identifier, "Expected group name after 'group'");
    consumeStatementTerminator();

    Program body = parseBlockUntil({TokenType::End});
    if (body.empty()) {
        throw ParseError("group '" + name.lexeme + "' must contain at least one statement in " +
                         name.filename + " on line " + std::to_string(name.line));
    }

    consume(TokenType::End, "Expected 'end' after group body");
    consumeStatementTerminator();
    return std::make_unique<GroupStmt>(name, std::move(body));
}

StmtPtr Parser::statement() {
    if (match({TokenType::Set})) {
        return setStatement();
    }

    if (match({TokenType::Print})) {
        return printStatement();
    }

    if (match({TokenType::Msg})) {
        return msgStatement();
    }

    if (match({TokenType::PlaySound})) {
        return playSoundStatement();
    }

    if (match({TokenType::Send})) {
        return sendStatement();
    }

    if (match({TokenType::Save})) {
        return saveStatement();
    }

    if (match({TokenType::Load})) {
        return loadStatement();
    }

    if (match({TokenType::Click})) {
        return clickStatement();
    }

    if (match({TokenType::MouseDown})) {
        return mouseDownStatement();
    }

    if (match({TokenType::MouseUp})) {
        return mouseUpStatement();
    }

    if (match({TokenType::MouseMove})) {
        return mouseMoveStatement();
    }

    if (match({TokenType::MouseHold})) {
        return mouseHoldStatement();
    }

    if (match({TokenType::Sleep})) {
        return sleepStatement();
    }

    if (match({TokenType::Toggle})) {
        return toggleStatement();
    }

    if (match({TokenType::Unloop})) {
        return unloopStatement();
    }

    if (match({TokenType::Loop})) {
        return loopStatement();
    }

    if (match({TokenType::If})) {
        return ifStatement();
    }

    if (check(TokenType::Identifier) && checkNext(TokenType::Equal)) {
        return assignmentStatement();
    }

    if (check(TokenType::Identifier) && isTgCommandName(peek().lexeme)) {
        return tgStatement();
    }

    return expressionStatement();
}

StmtPtr Parser::setStatement() {
    const Token name = consume(TokenType::Identifier, "Expected variable name after 'set'");
    consume(TokenType::Equal, "Expected '=' after variable name in set");
    auto value = expression();
    consumeStatementTerminator();
    return std::make_unique<AssignStmt>(name, std::move(value));
}

StmtPtr Parser::printStatement() {
    auto value = expression();
    consumeStatementTerminator();
    return std::make_unique<PrintStmt>(std::move(value));
}

StmtPtr Parser::msgStatement() {
    auto value = expression();
    consumeStatementTerminator();
    return std::make_unique<MsgStmt>(std::move(value));
}

StmtPtr Parser::playSoundStatement() {
    auto value = expression();
    consumeStatementTerminator();
    return std::make_unique<PlaySoundStmt>(std::move(value));
}

StmtPtr Parser::sendStatement() {
    std::vector<SendPart> parts;

    while (!isStatementTerminator()) {
        if (match({TokenType::LeftBrace})) {
            const Token key = consume(TokenType::Identifier, "Expected key name inside '{...}'");
            consume(TokenType::RightBrace, "Expected '}' after key name");
            parts.push_back(SendPart(key.lexeme));
            continue;
        }

        if (!startsExpression()) {
            throw ParseError("Expected text expression or '{Key}' in send in " + peek().filename +
                             " on line " + std::to_string(peek().line));
        }

        parts.push_back(SendPart(expression()));
    }

    if (parts.empty()) {
        throw ParseError("send must contain text or at least one '{Key}' in " + peek().filename +
                         " on line " + std::to_string(peek().line));
    }

    consumeStatementTerminator();
    return std::make_unique<SendStmt>(std::move(parts));
}

StmtPtr Parser::saveStatement() {
    auto path = expression();
    consumeStatementTerminator();
    return std::make_unique<SaveStmt>(std::move(path));
}

StmtPtr Parser::loadStatement() {
    auto path = expression();
    consumeStatementTerminator();
    return std::make_unique<LoadStmt>(std::move(path));
}

StmtPtr Parser::sleepStatement() {
    auto value = expression();
    consumeStatementTerminator();
    return std::make_unique<SleepStmt>(std::move(value));
}

StmtPtr Parser::clickStatement() {
    consumeStatementTerminator();
    return std::make_unique<ClickStmt>();
}

StmtPtr Parser::mouseDownStatement() {
    consumeStatementTerminator();
    return std::make_unique<MouseDownStmt>();
}

StmtPtr Parser::mouseUpStatement() {
    consumeStatementTerminator();
    return std::make_unique<MouseUpStmt>();
}

StmtPtr Parser::mouseMoveStatement() {
    // support syntax: mousemove, <x_expr>, <y_expr>
    if (match({TokenType::Comma})) {
        // optional comma after keyword
    }

    auto xExpr = expression();

    // require comma between coords
    consume(TokenType::Comma, "Expected ',' between mousemove coordinates");

    auto yExpr = expression();
    consumeStatementTerminator();
    return std::make_unique<MouseMoveStmt>(std::move(xExpr), std::move(yExpr));
}

StmtPtr Parser::mouseHoldStatement() {
    auto value = expression();
    consumeStatementTerminator();
    return std::make_unique<MouseHoldStmt>(std::move(value));
}

StmtPtr Parser::toggleStatement() {
    const Token name = consume(TokenType::Identifier, "Expected variable name after 'toggle'");
    consumeStatementTerminator();
    return std::make_unique<ToggleStmt>(name);
}

StmtPtr Parser::unloopStatement() {
    const int loopId = consumeLoopId();
    consumeStatementTerminator();
    return std::make_unique<UnloopStmt>(loopId);
}

StmtPtr Parser::loopStatement() {
    ExprPtr count;
    bool infinite = false;
    int loopId = 0;

    if (check(TokenType::LeftBrace)) {
        loopId = consumeLoopId();
    }

    if (isStatementTerminator()) {
        infinite = true;
    } else {
        count = expression();
    }

    consumeStatementTerminator();
    Program body = parseBlockUntil({TokenType::End});

    if (body.empty()) {
        throw ParseError("loop must contain at least one statement in " + peek().filename +
                         " on line " + std::to_string(peek().line));
    }

    consume(TokenType::End, "Expected 'end' after loop body");
    consumeStatementTerminator();
    return std::make_unique<LoopStmt>(std::move(count), std::move(body), infinite, loopId);
}

StmtPtr Parser::ifStatement() {
    auto condition = expression();
    consumeStatementTerminator();

    Program thenBody = parseBlockUntil({TokenType::Elif, TokenType::Else, TokenType::End});
    if (thenBody.empty()) {
        throw ParseError("if must contain at least one statement in " + peek().filename +
                         " on line " + std::to_string(peek().line));
    }

    std::vector<ConditionalBranch> elifBranches;
    while (match({TokenType::Elif})) {
        auto elifCondition = expression();
        consumeStatementTerminator();
        Program elifBody = parseBlockUntil({TokenType::Elif, TokenType::Else, TokenType::End});
        if (elifBody.empty()) {
            throw ParseError("elif must contain at least one statement in " + peek().filename +
                             " on line " + std::to_string(peek().line));
        }
        elifBranches.push_back(ConditionalBranch(std::move(elifCondition), std::move(elifBody)));
    }

    Program elseBody;
    if (match({TokenType::Else})) {
        consumeStatementTerminator();
        elseBody = parseBlockUntil({TokenType::End});
        if (elseBody.empty()) {
            throw ParseError("else must contain at least one statement in " + peek().filename +
                             " on line " + std::to_string(peek().line));
        }
    }

    consume(TokenType::End, "Expected 'end' after if block");
    consumeStatementTerminator();
    return std::make_unique<IfStmt>(std::move(condition), std::move(thenBody),
                                    std::move(elifBranches), std::move(elseBody));
}

StmtPtr Parser::assignmentStatement() {
    const Token name = consume(TokenType::Identifier, "Expected variable name");
    consume(TokenType::Equal, "Expected '=' in assignment");
    auto value = expression();
    consumeStatementTerminator();
    return std::make_unique<AssignStmt>(name, std::move(value));
}

StmtPtr Parser::expressionStatement() {
    auto expr = expression();
    consumeStatementTerminator();
    return std::make_unique<ExpressionStmt>(std::move(expr));
}

StmtPtr Parser::tgStatement() {
    const Token name = consume(TokenType::Identifier, "Expected tg command name");
    std::vector<ExprPtr> arguments;

    while (!isStatementTerminator()) {
        if (!startsExpression()) {
            throw ParseError("Expected an argument expression in " + name.lexeme + " in " +
                             peek().filename + " on line " + std::to_string(peek().line));
        }

        arguments.push_back(expression());
    }

    consumeStatementTerminator();
    return std::make_unique<TgStmt>(name, std::move(arguments));
}

ExprPtr Parser::expression() {
    return orExpression();
}

ExprPtr Parser::orExpression() {
    auto expr = andExpression();

    while (match({TokenType::Or})) {
        Token op = previous();
        auto right = andExpression();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }

    return expr;
}

ExprPtr Parser::andExpression() {
    auto expr = equality();

    while (match({TokenType::And})) {
        Token op = previous();
        auto right = equality();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }

    return expr;
}

ExprPtr Parser::equality() {
    auto expr = comparison();

    while (match({TokenType::BangEqual, TokenType::EqualEqual})) {
        Token op = previous();
        auto right = comparison();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }

    return expr;
}

ExprPtr Parser::comparison() {
    auto expr = term();

    while (match({TokenType::Greater, TokenType::GreaterEqual, TokenType::Less, TokenType::LessEqual})) {
        Token op = previous();
        auto right = term();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }

    return expr;
}

ExprPtr Parser::term() {
    auto expr = factor();

    while (match({TokenType::Plus, TokenType::Minus})) {
        Token op = previous();
        auto right = factor();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }

    return expr;
}

ExprPtr Parser::factor() {
    auto expr = unary();

    while (match({TokenType::Star, TokenType::Slash})) {
        Token op = previous();
        auto right = unary();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }

    return expr;
}

ExprPtr Parser::unary() {
    if (match({TokenType::Bang, TokenType::Minus})) {
        Token op = previous();
        auto right = unary();
        return std::make_unique<UnaryExpr>(op, std::move(right));
    }

    return call();
}

ExprPtr Parser::call() {
    auto expr = primary();

    while (match({TokenType::LeftParen})) {
        auto* variable = dynamic_cast<VariableExpr*>(expr.get());
        if (variable == nullptr) {
            throw ParseError("Only named functions can be called in " + previous().filename + " on line " +
                             std::to_string(previous().line));
        }

        std::vector<ExprPtr> arguments;
        if (!check(TokenType::RightParen)) {
            do {
                arguments.push_back(expression());
            } while (match({TokenType::Comma}));
        }

        consume(TokenType::RightParen, "Expected ')' after arguments");
        expr = std::make_unique<CallExpr>(variable->name, std::move(arguments));
    }

    return expr;
}

ExprPtr Parser::primary() {
    if (match({TokenType::False, TokenType::True, TokenType::Number, TokenType::String})) {
        return std::make_unique<LiteralExpr>(previous());
    }

    if (match({TokenType::Identifier})) {
        return std::make_unique<VariableExpr>(previous());
    }

    if (match({TokenType::LeftParen})) {
        auto expr = expression();
        consume(TokenType::RightParen, "Expected ')' after expression");
        return std::make_unique<GroupingExpr>(std::move(expr));
    }

    throw ParseError("Expected expression in " + peek().filename + " on line " + std::to_string(peek().line));
}

}  // namespace sek
