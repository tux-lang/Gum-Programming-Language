#include "Json.h"

#include <cstdint>
#include <unordered_map>
#include <utility>

#include "json.hpp"

namespace sek {

namespace {

std::unordered_map<int, nlohmann::json>& jsonStore() {
    static std::unordered_map<int, nlohmann::json> store;
    return store;
}

int& jsonCounter() {
    static int counter = 0;
    return counter;
}

const nlohmann::json* jsonByHandle(int handle) {
    const std::unordered_map<int, nlohmann::json>& store = jsonStore();
    const std::unordered_map<int, nlohmann::json>::const_iterator found = store.find(handle);
    return found == store.end() ? nullptr : &found->second;
}

// Converts a JSON value into a SekLang Value. Objects and arrays are copied
// into the global store and exposed as json handles.
Value jsonToValue(const nlohmann::json& json) {
    switch (json.type()) {
        case nlohmann::json::value_t::null:
            return Value();
        case nlohmann::json::value_t::boolean:
            return Value(json.get<bool>());
        case nlohmann::json::value_t::number_integer:
            return Value(static_cast<double>(json.get<std::int64_t>()));
        case nlohmann::json::value_t::number_unsigned:
            return Value(static_cast<double>(json.get<std::uint64_t>()));
        case nlohmann::json::value_t::number_float:
            return Value(json.get<double>());
        case nlohmann::json::value_t::string:
            return Value(json.get<std::string>());
        default: {
            const int handle = ++jsonCounter();
            jsonStore()[handle] = json;
            return Value::makeJson(handle);
        }
    }
}

}  // namespace

int jsonParse(const std::string& text) {
    try {
        nlohmann::json parsed = nlohmann::json::parse(text);
        const int handle = ++jsonCounter();
        jsonStore()[handle] = std::move(parsed);
        return handle;
    } catch (...) {
        return 0;
    }
}

std::string jsonTypeName(int handle) {
    const nlohmann::json* doc = jsonByHandle(handle);
    if (doc == nullptr) {
        return "null";
    }

    switch (doc->type()) {
        case nlohmann::json::value_t::null:
            return "null";
        case nlohmann::json::value_t::boolean:
            return "boolean";
        case nlohmann::json::value_t::number_integer:
        case nlohmann::json::value_t::number_unsigned:
        case nlohmann::json::value_t::number_float:
            return "number";
        case nlohmann::json::value_t::string:
            return "string";
        case nlohmann::json::value_t::array:
            return "array";
        case nlohmann::json::value_t::object:
            return "object";
    }

    return "unknown";
}

Value jsonGet(int handle, const std::string& key) {
    const nlohmann::json* doc = jsonByHandle(handle);
    if (doc == nullptr || !doc->is_object()) {
        return Value();
    }

    const nlohmann::json::const_iterator found = doc->find(key);
    if (found == doc->end()) {
        return Value();
    }

    return jsonToValue(*found);
}

Value jsonArrayLength(int handle) {
    const nlohmann::json* doc = jsonByHandle(handle);
    if (doc == nullptr || !doc->is_array()) {
        return Value();
    }

    return Value(static_cast<double>(doc->size()));
}

Value jsonArrayGet(int handle, int index) {
    const nlohmann::json* doc = jsonByHandle(handle);
    if (doc == nullptr || !doc->is_array()) {
        return Value();
    }

    if (index < 0 || static_cast<std::size_t>(index) >= doc->size()) {
        return Value();
    }

    return jsonToValue((*doc)[static_cast<std::size_t>(index)]);
}

std::string jsonValueToString(int handle) {
    const nlohmann::json* doc = jsonByHandle(handle);
    if (doc == nullptr) {
        return "<json:invalid>";
    }

    return doc->dump();
}

}  // namespace sek
