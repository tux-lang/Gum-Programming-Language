#pragma once

#include <string>

#include "Value.h"

namespace sek {

// Global store of parsed JSON documents. Each document is referenced by an
// integer handle returned from jsonParse and passed to the json_* helpers.
// Handles are stable for the lifetime of the process.

// Parses a JSON string and stores the result. Returns a handle (>= 1) on
// success, or 0 if the text is not valid JSON (callers map 0 to nil).
int jsonParse(const std::string& text);

// Returns the JSON type name of the document: "null", "boolean", "number",
// "string", "array" or "object". Invalid handles report "null".
std::string jsonTypeName(int handle);

// Extracts a value by key from an object document. Missing keys, non-object
// documents and invalid handles produce a nil Value. Nested objects and
// arrays are stored in the store and returned as json Values.
Value jsonGet(int handle, const std::string& key);

// Returns the element count of an array document as a number. Returns nil
// if the handle is invalid or the document is not an array.
Value jsonArrayLength(int handle);

// Returns the element at the given zero-based index of an array document.
// Returns nil if the handle is invalid, the document is not an array, or the
// index is out of range.
Value jsonArrayGet(int handle, int index);

// Serializes the JSON document to its compact string form (used by print).
std::string jsonValueToString(int handle);

}  // namespace sek
