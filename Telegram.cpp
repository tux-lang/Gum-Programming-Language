#include "Telegram.h"

#include <cstdint>
#include <cstdio>
#include <cmath>

#include "Http.h"
#include "WindowsPlatform.h"
#include "json.hpp"

namespace sek {

namespace {

// Serializes a number as a JSON value without trailing ".0" for integers.
std::string numberToJson(double value) {
    if (value == std::floor(value) && std::fabs(value) < 9.0e15) {
        return std::to_string(static_cast<long long>(value));
    }

    return std::to_string(value);
}

// Escapes a string for embedding into a JSON request body.
std::string jsonEscape(const std::string& text) {
    std::string escaped;
    for (std::string::const_iterator it = text.begin(); it != text.end(); ++it) {
        const char c = *it;
        switch (c) {
            case '"':
                escaped += "\\\"";
                break;
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
            case '\b':
                escaped += "\\b";
                break;
            case '\f':
                escaped += "\\f";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                                  static_cast<unsigned int>(static_cast<unsigned char>(c)));
                    escaped += buffer;
                } else {
                    escaped += c;
                }
                break;
        }
    }

    return escaped;
}

}  // namespace

std::string Telegram::apiUrl(const std::string& method) const {
    return "https://api.telegram.org/bot" + token_ + "/" + method;
}

Telegram::ApiResult Telegram::call(const std::string& method, const std::string& jsonBody) {
    ApiResult result;
    const HttpResponse response = httpPostWithStatus(apiUrl(method), jsonBody, "application/json");
    if (response.statusCode == 0) {
        return result;  // transport failure; the HTTP layer already logged it
    }

    result.transportOk = true;
    result.statusCode = response.statusCode;
    result.body = response.body;

    if (response.body.empty()) {
        return result;
    }

    try {
        const nlohmann::json parsed = nlohmann::json::parse(response.body);
        result.ok = parsed.value("ok", false);
        result.description = parsed.value("description", std::string());
    } catch (...) {
        // Non-JSON body; ok stays false.
    }

    return result;
}

bool Telegram::setToken(const std::string& token) {
    if (token.empty()) {
        std::fprintf(stderr, "[gum] tg_token: token must not be empty\n");
        return false;
    }

    token_ = token;
    const ApiResult result = call("getMe", "{}");
    if (!result.transportOk || !result.ok) {
        std::string reason = "network error";
        if (result.transportOk) {
            reason = result.description.empty()
                ? ("HTTP status " + std::to_string(result.statusCode))
                : result.description;
        }

        std::fprintf(stderr, "[gum] tg_token: invalid token or bot unavailable (%s)\n", reason.c_str());
        tokenValidated_ = false;
        return false;
    }

    tokenValidated_ = true;
    return true;
}

double Telegram::textHash(const std::string& text) {
    uint32_t hash = 2166136261u;
    for (std::string::const_iterator it = text.begin(); it != text.end(); ++it) {
        hash ^= static_cast<unsigned char>(*it);
        hash *= 16777619u;
    }

    return static_cast<double>(hash);
}

void Telegram::addStaticReply(const double commandHash, const std::string& reply) {
    staticReplies_[commandHash] = reply;
}

const std::string* Telegram::staticReplyFor(const double commandHash) const {
    const std::unordered_map<double, std::string>::const_iterator found =
        staticReplies_.find(commandHash);
    if (found == staticReplies_.end()) {
        return nullptr;
    }

    return &found->second;
}

int Telegram::fetchUpdates(std::vector<TelegramUpdate>& out) {
    std::string body = "{\"timeout\":30,\"offset\":";
    body += numberToJson(offset_);
    body += ",\"allowed_updates\":[\"message\",\"callback_query\"]}";

    const ApiResult result = call("getUpdates", body);
    if (!result.transportOk) {
        // Transient network failure: keep polling, the Vm loop paces retries.
        std::fprintf(stderr, "[gum] telegram getUpdates: network error, retrying\n");
        return 1;
    }

    if (result.statusCode == 409) {
        std::fprintf(stderr, "[gum] telegram getUpdates: 409 Conflict - another instance of this "
                             "bot is already running. Stopping.\n");
        return -1;
    }

    if (result.statusCode == 429) {
        long waitSeconds = 1;
        if (!result.body.empty()) {
            try {
                const nlohmann::json parsed = nlohmann::json::parse(result.body);
                const long retry = parsed.value("parameters", nlohmann::json())
                                       .value("retry_after", 0L);
                if (retry > 0) {
                    waitSeconds = retry;
                }
            } catch (...) {
            }
        }

        std::fprintf(stderr, "[gum] telegram getUpdates: 429 Too Many Requests - retrying in %ld s\n",
                     waitSeconds);
        preciseSleepMs(waitSeconds * 1000.0);
        return 1;
    }

    if (result.statusCode >= 500) {
        std::fprintf(stderr, "[gum] telegram getUpdates: HTTP %ld - retrying\n", result.statusCode);
        return 1;
    }

    if (!result.ok) {
        std::string reason = result.description.empty()
            ? ("HTTP status " + std::to_string(result.statusCode))
            : result.description;
        std::fprintf(stderr, "[gum] telegram getUpdates failed: %s\n", reason.c_str());
        return -1;
    }

    try {
        const nlohmann::json parsed = nlohmann::json::parse(result.body);
        const nlohmann::json& updateList = parsed["result"];
        if (!updateList.is_array()) {
            std::fprintf(stderr, "[gum] telegram getUpdates: unexpected response format\n");
            return -1;
        }

        double maxUpdateId = 0;
        for (const nlohmann::json& item : updateList) {
            const double updateId = item.value("update_id", 0.0);
            if (updateId > maxUpdateId) {
                maxUpdateId = updateId;
            }

            const nlohmann::json::const_iterator messageIt = item.find("message");
            if (messageIt != item.end() && messageIt->is_object()) {
                const nlohmann::json& message = *messageIt;
                TelegramUpdate update;
                update.updateId = updateId;
                update.chatId = message["chat"].value("id", 0.0);
                update.messageId = message.value("message_id", 0.0);

                const nlohmann::json::const_iterator textIt = message.find("text");
                if (textIt != message.end() && textIt->is_string()) {
                    update.hasText = true;
                    update.text = textIt->get<std::string>();
                }

                out.push_back(update);
            }

            const nlohmann::json::const_iterator callbackIt = item.find("callback_query");
            if (callbackIt != item.end() && callbackIt->is_object()) {
                const nlohmann::json& callback = *callbackIt;
                TelegramUpdate update;
                update.isCallback = true;
                update.updateId = updateId;
                update.callbackId = callback.value("id", std::string());

                const nlohmann::json::const_iterator dataIt = callback.find("data");
                if (dataIt != callback.end() && dataIt->is_string()) {
                    update.hasCallbackData = true;
                    update.callbackData = dataIt->get<std::string>();
                }

                const nlohmann::json::const_iterator messageIt2 = callback.find("message");
                if (messageIt2 != callback.end() && messageIt2->is_object()) {
                    const nlohmann::json& message = *messageIt2;
                    update.chatId = message["chat"].value("id", 0.0);
                    update.messageId = message.value("message_id", 0.0);
                }

                out.push_back(update);
            }
        }

        if (maxUpdateId > 0) {
            offset_ = maxUpdateId + 1;
        }
    } catch (...) {
        std::fprintf(stderr, "[gum] telegram getUpdates: failed to parse response\n");
        return -1;
    }

    return 0;
}

bool Telegram::sendMessage(const double chatId,
                           const std::string& text,
                           const bool replyTo,
                           const double replyToMessageId,
                           double& outMessageId) {
    outMessageId = 0;
    std::string body = "{\"chat_id\":";
    body += numberToJson(chatId);
    body += ",\"text\":\"";
    body += jsonEscape(text);
    body += "\"";
    if (replyTo) {
        body += ",\"reply_to_message_id\":";
        body += numberToJson(replyToMessageId);
    }
    body += "}";

    const ApiResult result = call("sendMessage", body);
    if (!result.transportOk || !result.ok) {
        std::fprintf(stderr, "[gum] telegram sendMessage failed: %s\n",
                     result.description.c_str());
        return false;
    }

    try {
        const nlohmann::json parsed = nlohmann::json::parse(result.body);
        outMessageId = parsed["result"].value("message_id", 0.0);
    } catch (...) {
    }

    return true;
}

bool Telegram::sendChatAction(const double chatId, const std::string& action) {
    std::string body = "{\"chat_id\":";
    body += numberToJson(chatId);
    body += ",\"action\":\"";
    body += jsonEscape(action);
    body += "\"}";

    const ApiResult result = call("sendChatAction", body);
    if (!result.transportOk || !result.ok) {
        std::fprintf(stderr, "[gum] telegram sendChatAction failed: %s\n",
                     result.description.c_str());
        return false;
    }

    return true;
}

bool Telegram::sendPhoto(const double chatId, const std::string& file) {
    std::string body = "{\"chat_id\":";
    body += numberToJson(chatId);
    body += ",\"photo\":\"";
    body += jsonEscape(file);
    body += "\"}";

    const ApiResult result = call("sendPhoto", body);
    if (!result.transportOk || !result.ok) {
        std::fprintf(stderr, "[gum] telegram sendPhoto failed: %s\n", result.description.c_str());
        return false;
    }

    return true;
}

bool Telegram::sendSticker(const double chatId, const std::string& file) {
    std::string body = "{\"chat_id\":";
    body += numberToJson(chatId);
    body += ",\"sticker\":\"";
    body += jsonEscape(file);
    body += "\"}";

    const ApiResult result = call("sendSticker", body);
    if (!result.transportOk || !result.ok) {
        std::fprintf(stderr, "[gum] telegram sendSticker failed: %s\n", result.description.c_str());
        return false;
    }

    return true;
}

bool Telegram::deleteMessage(const double chatId, const double messageId) {
    std::string body = "{\"chat_id\":";
    body += numberToJson(chatId);
    body += ",\"message_id\":";
    body += numberToJson(messageId);
    body += "}";

    const ApiResult result = call("deleteMessage", body);
    if (!result.transportOk || !result.ok) {
        std::fprintf(stderr, "[gum] telegram deleteMessage failed: %s\n",
                     result.description.c_str());
        return false;
    }

    return true;
}

bool Telegram::editMessageText(const double chatId,
                               const double messageId,
                               const std::string& text) {
    std::string body = "{\"chat_id\":";
    body += numberToJson(chatId);
    body += ",\"message_id\":";
    body += numberToJson(messageId);
    body += ",\"text\":\"";
    body += jsonEscape(text);
    body += "\"}";

    const ApiResult result = call("editMessageText", body);
    if (!result.transportOk || !result.ok) {
        std::fprintf(stderr, "[gum] telegram editMessageText failed: %s\n",
                     result.description.c_str());
        return false;
    }

    return true;
}

bool Telegram::answerCallbackQuery(const std::string& callbackId, const std::string& text) {
    std::string body = "{\"callback_query_id\":\"";
    body += jsonEscape(callbackId);
    body += "\",\"text\":\"";
    body += jsonEscape(text);
    body += "\"}";

    const ApiResult result = call("answerCallbackQuery", body);
    if (!result.transportOk || !result.ok) {
        std::fprintf(stderr, "[gum] telegram answerCallbackQuery failed: %s\n",
                     result.description.c_str());
        return false;
    }

    return true;
}

bool Telegram::getChat(const double chatId) {
    const std::string body = "{\"chat_id\":" + numberToJson(chatId) + "}";
    const ApiResult result = call("getChat", body);
    if (!result.transportOk || !result.ok) {
        std::fprintf(stderr, "[gum] telegram getChat failed: %s\n", result.description.c_str());
        return false;
    }

    try {
        const nlohmann::json parsed = nlohmann::json::parse(result.body);
        const nlohmann::json& chat = parsed["result"];
        const std::string type = chat.value("type", std::string());
        const std::string title = chat.value("title", std::string());
        const std::string firstName = chat.value("first_name", std::string());
        const std::string name = title.empty() ? firstName : title;
        std::fprintf(stdout, "[gum] chat %lld (%s): %s\n",
                     static_cast<long long>(chatId), type.c_str(), name.c_str());
    } catch (...) {
    }

    return true;
}

bool Telegram::leaveChat(const double chatId) {
    const std::string body = "{\"chat_id\":" + numberToJson(chatId) + "}";
    const ApiResult result = call("leaveChat", body);
    if (!result.transportOk || !result.ok) {
        std::fprintf(stderr, "[gum] telegram leaveChat failed: %s\n", result.description.c_str());
        return false;
    }

    return true;
}

}  // namespace sek
