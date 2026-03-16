#pragma once

// Application settings persistence.
// Settings are stored in %APPDATA%/claude-usage-bar/settings.json

#include <filesystem>
#include <string>

namespace settings {

// ============================================================
// Settings data
// ============================================================

struct AppSettings {
    int polling_minutes = 30;
    bool setup_complete = false;
    bool launch_at_login = false;

    // Notification thresholds (0 = off, 5-100 = percentage)
    int threshold_5h = 0;
    int threshold_7d = 0;
    int threshold_extra = 0;
};

// ============================================================
// File I/O
// ============================================================

std::filesystem::path get_settings_file_path();

AppSettings load_settings();
AppSettings load_settings(const std::filesystem::path& path);

bool save_settings(const AppSettings& s);
bool save_settings(const AppSettings& s, const std::filesystem::path& path);

// ============================================================
// Windows startup registration
// ============================================================

// Register/unregister in HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Run
bool set_launch_at_login(bool enabled);
bool get_launch_at_login();

}  // namespace settings
