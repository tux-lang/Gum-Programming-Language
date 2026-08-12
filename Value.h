#pragma once

#include <cstddef>
#include <sstream>
#include <string>

namespace sek {

// JSON value printed in place of a JSON handle (defined in Json.cpp).
std::string jsonValueToString(int handle);

class Value {
public:
    enum class Type {
        Nil,
        Number,
        Boolean,
        String,
        Json
    };

    // Field layout offsets used by the JIT code generator.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static std::size_t jitTypeOffset() { return offsetof(Value, type_); }
    static std::size_t jitNumberOffset() { return offsetof(Value, number_); }
    static std::size_t jitSize() { return sizeof(Value); }
#pragma GCC diagnostic pop

    Value() = default;
    Value(double numberValue) : type_(Type::Number), number_(numberValue) {}
    Value(bool booleanValue) : type_(Type::Boolean), boolean_(booleanValue) {}
    Value(std::string stringValue) : type_(Type::String), string_(std::move(stringValue)) {}
    Value(const char* stringValue) : type_(Type::String), string_(stringValue) {}

    // Creates a value that holds a JSON document handle (id into the global store).
    // The handle is stored in the numeric field so the JIT-visible layout is unchanged.
    static Value makeJson(int jsonHandle) {
        Value value;
        value.type_ = Type::Json;
        value.number_ = static_cast<double>(jsonHandle);
        return value;
    }

    Type type() const { return type_; }
    bool isNil() const { return type_ == Type::Nil; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isBoolean() const { return type_ == Type::Boolean; }
    bool isString() const { return type_ == Type::String; }
    bool isJson() const { return type_ == Type::Json; }

    double asNumber() const { return number_; }
    bool asBoolean() const { return boolean_; }
    const std::string& asString() const { return string_; }
    int asJsonHandle() const { return static_cast<int>(number_); }

    bool operator==(const Value& other) const {
        if (type_ != other.type_) {
            return false;
        }

        switch (type_) {
            case Type::Nil:
                return true;
            case Type::Number:
                return number_ == other.number_;
            case Type::Boolean:
                return boolean_ == other.boolean_;
            case Type::String:
                return string_ == other.string_;
            case Type::Json:
                return number_ == other.number_;
        }

        return false;
    }

private:
    Type type_ = Type::Nil;
    double number_ = 0.0;
    bool boolean_ = false;
    std::string string_;
};

inline std::string valueToString(const Value& value) {
    if (value.isNil()) {
        return "nil";
    }

    if (value.isNumber()) {
        std::ostringstream stream;
        stream << value.asNumber();
        return stream.str();
    }

    if (value.isBoolean()) {
        return value.asBoolean() ? "true" : "false";
    }

    if (value.isJson()) {
        return jsonValueToString(value.asJsonHandle());
    }

    return value.asString();
}

inline bool isTruthy(const Value& value) {
    if (value.isNil()) {
        return false;
    }

    if (value.isBoolean()) {
        return value.asBoolean();
    }

    if (value.isNumber()) {
        return value.asNumber() != 0.0;
    }

    if (value.isJson()) {
        return true;
    }

    return !value.asString().empty();
}

}  // namespace sek
