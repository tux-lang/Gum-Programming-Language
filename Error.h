#pragma once

#include <stdexcept>
#include <string>

namespace sek {

class SekError : public std::runtime_error {
public:
    explicit SekError(const std::string& message);
};

class ParseError : public SekError {
public:
    explicit ParseError(const std::string& message);
};

class RuntimeError : public SekError {
public:
    explicit RuntimeError(const std::string& message);
};

}  // namespace sek
