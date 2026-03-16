#pragma once

#include <string>
#include <windows.h>
#include <rpc.h>

namespace string_utils {

// Convert UTF-8 std::string to UTF-16 std::wstring for Win32 API calls
inline std::wstring utf8_to_wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int size = MultiByteToWideChar(
        CP_UTF8, 0,
        utf8.data(), static_cast<int>(utf8.size()),
        nullptr, 0
    );
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0,
        utf8.data(), static_cast<int>(utf8.size()),
        result.data(), size
    );
    return result;
}

// Convert UTF-16 std::wstring to UTF-8 std::string for JSON/HTTP
inline std::string wide_to_utf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int size = WideCharToMultiByte(
        CP_UTF8, 0,
        wide.data(), static_cast<int>(wide.size()),
        nullptr, 0, nullptr, nullptr
    );
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0,
        wide.data(), static_cast<int>(wide.size()),
        result.data(), size, nullptr, nullptr
    );
    return result;
}

// Generate a UUID v4 string (e.g. "550e8400-e29b-41d4-a716-446655440000")
inline std::string generate_uuid() {
    UUID uuid;
    UuidCreate(&uuid);
    RPC_CSTR str = nullptr;
    if (UuidToStringA(&uuid, &str) != RPC_S_OK) return {};
    std::string result(reinterpret_cast<const char*>(str));
    RpcStringFreeA(&str);
    return result;
}

}  // namespace string_utils
