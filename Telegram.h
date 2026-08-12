#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sek {

// A single update received from getUpdates. Only the fields a gum bot needs
// are extracted; everything else is discarded.
struct TelegramUpdate {
    double updateId = 0;

    bool isCallback = false;

    // Message fields (also filled from callback_query.message for callbacks).
    double chatId = 0;
    double messageId = 0;
    bool hasText = false;
    std::string text;

    // Callback-only fields.
    bool hasCallbackData = false;
    std::string callbackData;
    std::string callbackId;
};

// Thin wrapper around the Telegram Bot HTTP API. All methods are best-effort:
// failures are logged to stderr and reported via the return value, they never
// throw. The class owns the poll offset and the tg_on static reply table.
class Telegram {
public:
    Telegram() = default;

    // Validates the token via getMe. Returns true when the token is usable.
    // On failure prints a diagnostic to stderr and returns false.
    bool setToken(const std::string& token);

    bool hasToken() const { return tokenValidated_; }
    const std::string& token() const { return token_; }

    // FNV-1a 32-bit hash, returned as a number. Used for tg_cmd_hash and for
    // TG_TEXT / TG_CALLBACK_DATA values.
    static double textHash(const std::string& text);

    // Registers a static reply for a command text hash (tg_on).
    void addStaticReply(double commandHash, const std::string& reply);

    // Returns the registered reply for the command hash, or nullptr.
    const std::string* staticReplyFor(double commandHash) const;

    // Performs one long-poll getUpdates request (timeout = 30 s) and parses
    // the updates into `out`.
    // Returns 0 on success (updates available, possibly none),
    //         1 when the caller should pause briefly and retry (429 etc.),
    //        -1 on fatal errors (409 conflict, transport failure, bad token).
    int fetchUpdates(std::vector<TelegramUpdate>& out);

    // API methods. Each returns true on success (ok == true). Diagnostics go
    // to stderr on failure.
    bool sendMessage(double chatId,
                     const std::string& text,
                     bool replyTo,
                     double replyToMessageId,
                     double& outMessageId);
    bool sendChatAction(double chatId, const std::string& action);
    bool sendPhoto(double chatId, const std::string& file);
    bool sendSticker(double chatId, const std::string& file);
    bool deleteMessage(double chatId, double messageId);
    bool editMessageText(double chatId, double messageId, const std::string& text);
    bool answerCallbackQuery(const std::string& callbackId, const std::string& text);
    bool getChat(double chatId);
    bool leaveChat(double chatId);

private:
    struct ApiResult {
        bool transportOk = false;  // HTTP request completed (any status code)
        long statusCode = 0;       // 0 when the transport failed
        bool ok = false;           // JSON "ok" field
        std::string description;
        std::string body;          // raw response body
    };

    ApiResult call(const std::string& method, const std::string& jsonBody);
    std::string apiUrl(const std::string& method) const;

    std::string token_;
    bool tokenValidated_ = false;
    double offset_ = 0;
    std::unordered_map<double, std::string> staticReplies_;
};

}  // namespace sek
