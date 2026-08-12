#pragma once

#include <string>

namespace sek {

// Perform a synchronous GET request to the given URL.
// Returns the response body as a string, or an empty string on failure
// (network error, timeout, or an HTTP status >= 400). A diagnostic line is
// printed to stderr on failure but the calling script is not aborted.
std::string httpGet(const std::string& url);

// Perform a synchronous POST request with the given request body.
// content_type sets the "Content-Type" header (use e.g. "application/json"
// or "text/plain"). Returns the response body as a string, or an empty
// string on failure. Diagnostics go to stderr; the script continues.
std::string httpPost(const std::string& url,
                     const std::string& body,
                     const std::string& contentType);

// Result of a request that reports the HTTP status alongside the body.
struct HttpResponse {
    long statusCode = 0;  // 0 means the request itself failed
    std::string body;
};

// Like httpPost, but returns the response body even for HTTP status codes
// >= 400 (the status code is reported so callers can react to error bodies,
// e.g. Telegram's 409/429 responses). On a network-level failure statusCode
// is 0 and the body is empty; diagnostics go to stderr.
HttpResponse httpPostWithStatus(const std::string& url,
                                const std::string& body,
                                const std::string& contentType);

}  // namespace sek
