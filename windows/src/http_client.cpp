// HTTP client implementation using WinHTTP.
// All HINTERNET handles are managed via win32_raii::UniqueHInternet.

#include "http_client.h"
#include "string_utils.h"
#include "win32_raii.h"

#include <windows.h>
#include <winhttp.h>

#include <string>
#include <vector>

namespace http_client {

namespace {

struct ParsedUrl {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    bool is_https = true;
};

std::optional<ParsedUrl> parse_url(const std::string& url) {
    const std::wstring wide_url = string_utils::utf8_to_wide(url);

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);

    // Request host and path buffers
    wchar_t host_buf[256]{};
    wchar_t path_buf[2048]{};
    components.lpszHostName = host_buf;
    components.dwHostNameLength = static_cast<DWORD>(std::size(host_buf));
    components.lpszUrlPath = path_buf;
    components.dwUrlPathLength = static_cast<DWORD>(std::size(path_buf));

    if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &components)) {
        return std::nullopt;
    }

    ParsedUrl result;
    result.host = std::wstring(components.lpszHostName, components.dwHostNameLength);
    result.path = std::wstring(components.lpszUrlPath, components.dwUrlPathLength);
    result.port = components.nPort;
    result.is_https = (components.nScheme == INTERNET_SCHEME_HTTPS);
    return result;
}

std::string read_response_body(HINTERNET request) {
    std::string body;
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
        std::vector<char> buf(available);
        DWORD read = 0;
        if (!WinHttpReadData(request, buf.data(), available, &read)) {
            break;
        }
        body.append(buf.data(), read);
    }
    return body;
}

int read_status_code(HINTERNET request) {
    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        return 0;
    }
    return static_cast<int>(status);
}

std::map<std::string, std::string> read_response_headers(HINTERNET request) {
    std::map<std::string, std::string> headers;

    // Query total header size
    DWORD buf_len = 0;
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_RAW_HEADERS_CRLF,
        WINHTTP_HEADER_NAME_BY_INDEX,
        WINHTTP_NO_OUTPUT_BUFFER, &buf_len, WINHTTP_NO_HEADER_INDEX);

    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || buf_len == 0) {
        return headers;
    }

    std::vector<wchar_t> buf(buf_len / sizeof(wchar_t));
    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_RAW_HEADERS_CRLF,
            WINHTTP_HEADER_NAME_BY_INDEX,
            buf.data(), &buf_len, WINHTTP_NO_HEADER_INDEX)) {
        return headers;
    }

    // Parse "Name: Value\r\n" lines
    std::wstring raw(buf.data(), buf_len / sizeof(wchar_t));
    size_t pos = 0;
    while (pos < raw.size()) {
        const auto line_end = raw.find(L"\r\n", pos);
        if (line_end == std::wstring::npos) break;

        const auto line = raw.substr(pos, line_end - pos);
        pos = line_end + 2;

        const auto colon = line.find(L':');
        if (colon == std::wstring::npos) continue;

        auto name = string_utils::wide_to_utf8(line.substr(0, colon));
        auto value = string_utils::wide_to_utf8(line.substr(colon + 1));

        // Trim leading whitespace from value
        const auto first_nonspace = value.find_first_not_of(" \t");
        if (first_nonspace != std::string::npos) {
            value = value.substr(first_nonspace);
        }

        // Store header name in lowercase for consistent lookup
        for (auto& c : name) {
            if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        }

        headers[name] = value;
    }

    return headers;
}

}  // namespace

std::optional<HttpResponse> winhttp_request(
    const std::string& url,
    const std::string& method,
    const std::map<std::string, std::string>& headers,
    const std::string& body) {

    const auto parsed = parse_url(url);
    if (!parsed) return std::nullopt;

    auto session = win32_raii::make_hinternet(
        WinHttpOpen(
            L"ClaudeUsageBar/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0));
    if (!session) return std::nullopt;

    auto connection = win32_raii::make_hinternet(
        WinHttpConnect(
            session.get(),
            parsed->host.c_str(),
            parsed->port,
            0));
    if (!connection) return std::nullopt;

    const std::wstring wide_method = string_utils::utf8_to_wide(method);
    const DWORD flags = parsed->is_https ? WINHTTP_FLAG_SECURE : 0;

    auto request = win32_raii::make_hinternet(
        WinHttpOpenRequest(
            connection.get(),
            wide_method.c_str(),
            parsed->path.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            flags));
    if (!request) return std::nullopt;

    // Add request headers
    for (const auto& [key, value] : headers) {
        const std::wstring header_line =
            string_utils::utf8_to_wide(key + ": " + value);
        WinHttpAddRequestHeaders(
            request.get(),
            header_line.c_str(),
            static_cast<DWORD>(header_line.size()),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    // Send request
    LPVOID body_ptr = body.empty()
        ? WINHTTP_NO_REQUEST_DATA
        : static_cast<LPVOID>(const_cast<char*>(body.data()));
    const DWORD body_len = static_cast<DWORD>(body.size());

    if (!WinHttpSendRequest(
            request.get(),
            WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            body_ptr, body_len, body_len, 0)) {
        return std::nullopt;
    }

    if (!WinHttpReceiveResponse(request.get(), nullptr)) {
        return std::nullopt;
    }

    HttpResponse response;
    response.status_code = read_status_code(request.get());
    response.headers = read_response_headers(request.get());
    response.body = read_response_body(request.get());

    return response;
}

}  // namespace http_client
