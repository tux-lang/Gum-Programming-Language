#include "Environment.h"

namespace sek {

void Environment::define(const std::string& name, const Value& value) {
    values_[name] = value;
}

void Environment::assign(const std::string& name, const Value& value) {
    const auto iterator = values_.find(name);
    if (iterator == values_.end()) {
        throw RuntimeError("Undefined variable: " + name);
    }

    iterator->second = value;
}

Value Environment::get(const std::string& name) const {
    const auto iterator = values_.find(name);
    if (iterator == values_.end()) {
        throw RuntimeError("Undefined variable: " + name);
    }

    return iterator->second;
}

const std::unordered_map<std::string, Value>& Environment::values() const {
    return values_;
}

}  // namespace sek
