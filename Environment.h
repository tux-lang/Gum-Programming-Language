#pragma once

#include <string>
#include <unordered_map>

#include "Value.h"
#include "Error.h"

namespace sek {

class Environment {
public:
    void define(const std::string& name, const Value& value);
    void assign(const std::string& name, const Value& value);
    Value get(const std::string& name) const;
    const std::unordered_map<std::string, Value>& values() const;

private:
    std::unordered_map<std::string, Value> values_;
};

}  // namespace sek
