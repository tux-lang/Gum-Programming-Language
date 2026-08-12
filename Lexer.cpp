#include "Lexer.h"

#include <unordered_map>

#include "Error.h"

namespace sek {

Lexer::Lexer(std::string source, std::string filename)
    : source_(std::move(source)), filename_(std::move(filename)) {}

std::vector<Token> Lexer::scanTokens() const {
    static const std::unordered_map<std::string, TokenType> keywords = {
        {"let", TokenType::Let},
        {"set", TokenType::Set},
        {"print", TokenType::Print},
        {"msg", TokenType::Msg},
        {"playsound", TokenType::PlaySound},
        {"send", TokenType::Send},
        {"save", TokenType::Save},
        {"load", TokenType::Load},
        {"click", TokenType::Click},
        {"mousemove", TokenType::MouseMove},
        {"mousedown", TokenType::MouseDown},
        {"mouseup", TokenType::MouseUp},
        {"mousehold", TokenType::MouseHold},
        {"sleep", TokenType::Sleep},
        {"toggle", TokenType::Toggle},
        {"loop", TokenType::Loop},
        {"unloop", TokenType::Unloop},
        {"if", TokenType::If},
        {"elif", TokenType::Elif},
        {"else", TokenType::Else},
        {"end", TokenType::End},
        {"and", TokenType::And},
        {"or", TokenType::Or},
        {"true", TokenType::True},
        {"false", TokenType::False},
        {"group", TokenType::Group},
        {"use", TokenType::Use},
    };

    std::vector<Token> tokens;
    std::size_t current = 0;
    int line = 1;

    auto addToken = [&](TokenType type, std::string lexeme) {
        tokens.push_back(Token{type, std::move(lexeme), line, filename_});
    };

    while (!isAtEnd(current, source_)) {
        const char ch = source_[current];

        switch (ch) {
            case '(':
                addToken(TokenType::LeftParen, "(");
                ++current;
                break;
            case ')':
                addToken(TokenType::RightParen, ")");
                ++current;
                break;
            case ',':
                addToken(TokenType::Comma, ",");
                ++current;
                break;
            case '{':
                addToken(TokenType::LeftBrace, "{");
                ++current;
                break;
            case '}':
                addToken(TokenType::RightBrace, "}");
                ++current;
                break;
            case ':':
                addToken(TokenType::Colon, ":");
                ++current;
                break;
            case '+':
                addToken(TokenType::Plus, "+");
                ++current;
                break;
            case '-':
                addToken(TokenType::Minus, "-");
                ++current;
                break;
            case '*':
                addToken(TokenType::Star, "*");
                ++current;
                break;
            case '/':
                addToken(TokenType::Slash, "/");
                ++current;
                break;
            case ';':
                addToken(TokenType::Semicolon, ";");
                ++current;
                break;
            case '\r':
                ++current;
                break;
            case '\n':
                addToken(TokenType::NewLine, "\\n");
                ++current;
                ++line;
                break;
            case ' ':
            case '\t':
                ++current;
                break;
            case '#':
                while (!isAtEnd(current, source_) && source_[current] != '\n') {
                    ++current;
                }
                break;
            case '!':
                if (current + 1 < source_.size() && source_[current + 1] == '=') {
                    addToken(TokenType::BangEqual, "!=");
                    current += 2;
                } else {
                    addToken(TokenType::Bang, "!");
                    ++current;
                }
                break;
            case '=':
                if (current + 1 < source_.size() && source_[current + 1] == '=') {
                    addToken(TokenType::EqualEqual, "==");
                    current += 2;
                } else {
                    addToken(TokenType::Equal, "=");
                    ++current;
                }
                break;
            case '>':
                if (current + 1 < source_.size() && source_[current + 1] == '=') {
                    addToken(TokenType::GreaterEqual, ">=");
                    current += 2;
                } else {
                    addToken(TokenType::Greater, ">");
                    ++current;
                }
                break;
            case '<':
                if (current + 1 < source_.size() && source_[current + 1] == '=') {
                    addToken(TokenType::LessEqual, "<=");
                    current += 2;
                } else {
                    addToken(TokenType::Less, "<");
                    ++current;
                }
                break;
            case '"': {
                ++current;
                std::string text;
                bool closed = false;

                while (!isAtEnd(current, source_)) {
                    const char ch = source_[current];
                    if (ch == '"') {
                        closed = true;
                        ++current;
                        break;
                    }

                    if (ch == '\\' && current + 1 < source_.size()) {
                        const char escaped = source_[current + 1];
                        switch (escaped) {
                            case '\\':
                                text += '\\';
                                break;
                            case '"':
                                text += '"';
                                break;
                            case 'n':
                                text += '\n';
                                break;
                            case 't':
                                text += '\t';
                                break;
                            case 'r':
                                text += '\r';
                                break;
                            default:
                                // Unknown escape: keep backslash and char as-is.
                                text += ch;
                                text += escaped;
                                break;
                        }
                        if (escaped == '\n') {
                            ++line;
                        }
                        current += 2;
                        continue;
                    }

                    if (ch == '\n') {
                        ++line;
                    }
                    text += ch;
                    ++current;
                }

                if (!closed) {
                    throw ParseError("Unterminated string in " + filename_ + " on line " + std::to_string(line));
                }

                addToken(TokenType::String, text);
                break;
            }
            default:
                if (isDigit(ch)) {
                    const auto start = current;
                    while (!isAtEnd(current, source_) && isDigit(source_[current])) {
                        ++current;
                    }

                    if (!isAtEnd(current, source_) && source_[current] == '.' &&
                        current + 1 < source_.size() && isDigit(source_[current + 1])) {
                        ++current;
                        while (!isAtEnd(current, source_) && isDigit(source_[current])) {
                            ++current;
                        }
                    }

                    addToken(TokenType::Number, source_.substr(start, current - start));
                    break;
                }

                if (isAlpha(ch)) {
                    const auto start = current;
                    while (!isAtEnd(current, source_) && isAlphaNumeric(source_[current])) {
                        ++current;
                    }

                    const std::string text = source_.substr(start, current - start);
                    const auto keyword = keywords.find(text);
                    if (keyword != keywords.end()) {
                        addToken(keyword->second, text);
                    } else {
                        addToken(TokenType::Identifier, text);
                    }
                    break;
                }

                throw ParseError("Unexpected character '" + std::string(1, ch) + "' in " + filename_ + " on line " +
                                 std::to_string(line));
        }
    }

    tokens.push_back(Token{TokenType::EndOfFile, "", line, filename_});
    return tokens;
}

bool Lexer::isAtEnd(const std::size_t current, const std::string& source) {
    return current >= source.size();
}

bool Lexer::isDigit(const char ch) {
    return ch >= '0' && ch <= '9';
}

bool Lexer::isAlpha(const char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

bool Lexer::isAlphaNumeric(const char ch) {
    return isAlpha(ch) || isDigit(ch);
}

}  // namespace sek
