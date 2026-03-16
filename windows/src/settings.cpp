// Application settings persistence.
// Settings are stored in %APPDATA%/claude-usage-bar/settings.json

#include "settings.h"
#include "config.h"
#include "oauth.h"
#include "string_utils.h"

#include <nlohmann/json.hpp>

#include <fstream>

#include <windows.h>

namespace settings {

// ============================================================
// File I/O
// ============================================================

std::filesystem::path get_settings_file_path() {
    return oauth::get_config_dir() / "settings.json";
}

namespace {

nlohmann::json settings_to_json(const AppSettings& s) {
    return nlohmann::json{
        {"polling_minutes",  s.polling_minutes},
        {"setup_complete",   s.setup_complete},
        {"launch_at_login",  s.launch_at_login},
        {"threshold_5h",     s.threshold_5h},
        {"threshold_7d",     s.threshold_7d},
        {"threshold_extra",  s.threshold_extra}
    };
}

AppSettings json_to_settings(const nlohmann::json& j) {
    AppSettings s;

    auto read_int = [&](const char* key, int& target) {
        auto it = j.find(key);
        if (it != j.end() && it->is_number_integer()) {
            target = it->get<int>();
        }
    };
    auto read_bool = [&](const char* key, bool& target) {
        auto it = j.find(key);
        if (it != j.end() && it->is_boolean()) {
            target = it->get<bool>();
        }
    };

    read_int("polling_minutes", s.polling_minutes);
    read_bool("setup_complete", s.setup_complete);
    read_bool("launch_at_login", s.launch_at_login);
    read_int("threshold_5h", s.threshold_5h);
    read_int("threshold_7d", s.threshold_7d);
    read_int("threshold_extra", s.threshold_extra);

    return s;
}

}  // anonymous namespace

AppSettings load_settings() {
    return load_settings(get_settings_file_path());
}

AppSettings load_settings(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return AppSettings{};
    }

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return AppSettings{};
    }

    try {
        nlohmann::json j = nlohmann::json::parse(ifs);
        return json_to_settings(j);
    } catch (...) {
        return AppSettings{};
    }
}

bool save_settings(const AppSettings& s) {
    return save_settings(s, get_settings_file_path());
}

bool save_settings(const AppSettings& s, const std::filesystem::path& path) {
    // Ensure parent directory exists
    auto parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) return false;
    }

    std::ofstream ofs(path, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) return false;

    ofs << settings_to_json(s).dump(2);
    return true;
}

// ============================================================
// Windows startup registration
// ============================================================

namespace {

const wchar_t* kRunKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kAppValueName = L"ClaudeUsageBar";

}  // anonymous namespace

bool set_launch_at_login(bool enabled) {
    HKEY key = nullptr;
    auto result = RegOpenKeyExW(
        HKEY_CURRENT_USER, kRunKey,
        0, KEY_SET_VALUE, &key
    );
    if (result != ERROR_SUCCESS) return false;

    bool success = false;
    if (enabled) {
        // Get the executable path
        wchar_t exe_path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe_path, MAX_PATH);

        result = RegSetValueExW(
            key, kAppValueName, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(exe_path),
            static_cast<DWORD>((wcslen(exe_path) + 1) * sizeof(wchar_t))
        );
        success = (result == ERROR_SUCCESS);
    } else {
        result = RegDeleteValueW(key, kAppValueName);
        success = (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    }

    RegCloseKey(key);
    return success;
}

bool get_launch_at_login() {
    HKEY key = nullptr;
    auto result = RegOpenKeyExW(
        HKEY_CURRENT_USER, kRunKey,
        0, KEY_QUERY_VALUE, &key
    );
    if (result != ERROR_SUCCESS) return false;

    DWORD type = 0;
    DWORD size = 0;
    result = RegQueryValueExW(key, kAppValueName, nullptr, &type, nullptr, &size);
    RegCloseKey(key);

    return (result == ERROR_SUCCESS && type == REG_SZ);
}

}  // namespace settings
