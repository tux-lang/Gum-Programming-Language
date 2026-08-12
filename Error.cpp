#include "Error.h"

namespace sek {

SekError::SekError(const std::string& message) : std::runtime_error(message) {}

ParseError::ParseError(const std::string& message) : SekError(message) {}

RuntimeError::RuntimeError(const std::string& message) : SekError(message) {}

}  // namespace sek
