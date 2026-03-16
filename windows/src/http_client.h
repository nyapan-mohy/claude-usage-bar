#pragma once

// HTTP client abstraction layer.
// Production uses WinHTTP; tests inject a mock via std::function.

#include <functional>
#include <map>
#include <optional>
#include <string>

namespace http_client {

struct HttpResponse {
    int status_code = 0;
    std::string body;
    std::map<std::string, std::string> headers;
};

// Function signature for making HTTP requests.
// Implementations: winhttp_request (production), or a lambda (tests).
using HttpRequestFn = std::function<std::optional<HttpResponse>(
    const std::string& url,
    const std::string& method,                       // "GET" or "POST"
    const std::map<std::string, std::string>& headers,
    const std::string& body
)>;

// Production implementation using WinHTTP.
// Returns nullopt on network/connection failure.
std::optional<HttpResponse> winhttp_request(
    const std::string& url,
    const std::string& method,
    const std::map<std::string, std::string>& headers,
    const std::string& body);

}  // namespace http_client
