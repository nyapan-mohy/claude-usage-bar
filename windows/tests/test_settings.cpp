// Tests for settings persistence.

#include <gtest/gtest.h>
#include "settings.h"

#include <filesystem>
#include <fstream>

#include <windows.h>

namespace {

struct TempDir {
    std::filesystem::path path;

    TempDir() {
        wchar_t tmp[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tmp);
        path = std::filesystem::path(tmp) / "settings_test";
        std::filesystem::create_directories(path);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

}  // anonymous namespace

// ============================================================
// Default values
// ============================================================

TEST(AppSettings, DefaultValues) {
    settings::AppSettings s;
    EXPECT_EQ(s.polling_minutes, 30);
    EXPECT_FALSE(s.setup_complete);
    EXPECT_FALSE(s.launch_at_login);
    EXPECT_EQ(s.threshold_5h, 0);
    EXPECT_EQ(s.threshold_7d, 0);
    EXPECT_EQ(s.threshold_extra, 0);
}

// ============================================================
// Save / Load round trip
// ============================================================

TEST(SettingsIO, SaveAndLoadRoundTrip) {
    TempDir tmp;
    auto file = tmp.path / "settings.json";

    settings::AppSettings s;
    s.polling_minutes = 15;
    s.setup_complete = true;
    s.launch_at_login = true;
    s.threshold_5h = 80;
    s.threshold_7d = 90;
    s.threshold_extra = 50;

    ASSERT_TRUE(settings::save_settings(s, file));

    auto loaded = settings::load_settings(file);
    EXPECT_EQ(loaded.polling_minutes, 15);
    EXPECT_TRUE(loaded.setup_complete);
    EXPECT_TRUE(loaded.launch_at_login);
    EXPECT_EQ(loaded.threshold_5h, 80);
    EXPECT_EQ(loaded.threshold_7d, 90);
    EXPECT_EQ(loaded.threshold_extra, 50);
}

TEST(SettingsIO, LoadFromNonExistentFileReturnsDefaults) {
    TempDir tmp;
    auto file = tmp.path / "nonexistent.json";

    auto loaded = settings::load_settings(file);
    EXPECT_EQ(loaded.polling_minutes, 30);
    EXPECT_FALSE(loaded.setup_complete);
    EXPECT_FALSE(loaded.launch_at_login);
}

TEST(SettingsIO, LoadFromInvalidJsonReturnsDefaults) {
    TempDir tmp;
    auto file = tmp.path / "bad.json";

    std::ofstream ofs(file);
    ofs << "this is not json";
    ofs.close();

    auto loaded = settings::load_settings(file);
    EXPECT_EQ(loaded.polling_minutes, 30);
    EXPECT_FALSE(loaded.setup_complete);
}

TEST(SettingsIO, PartialJsonUsesDefaults) {
    TempDir tmp;
    auto file = tmp.path / "partial.json";

    std::ofstream ofs(file);
    ofs << R"({"polling_minutes": 5})";
    ofs.close();

    auto loaded = settings::load_settings(file);
    EXPECT_EQ(loaded.polling_minutes, 5);
    // Other fields should be defaults
    EXPECT_FALSE(loaded.setup_complete);
    EXPECT_EQ(loaded.threshold_5h, 0);
}

TEST(SettingsIO, SaveCreatesParentDirectory) {
    TempDir tmp;
    auto file = tmp.path / "subdir" / "settings.json";

    settings::AppSettings s;
    s.polling_minutes = 60;

    ASSERT_TRUE(settings::save_settings(s, file));
    EXPECT_TRUE(std::filesystem::exists(file));

    auto loaded = settings::load_settings(file);
    EXPECT_EQ(loaded.polling_minutes, 60);
}

TEST(SettingsIO, OverwriteExistingFile) {
    TempDir tmp;
    auto file = tmp.path / "settings.json";

    settings::AppSettings s1;
    s1.polling_minutes = 15;
    ASSERT_TRUE(settings::save_settings(s1, file));

    settings::AppSettings s2;
    s2.polling_minutes = 60;
    s2.setup_complete = true;
    ASSERT_TRUE(settings::save_settings(s2, file));

    auto loaded = settings::load_settings(file);
    EXPECT_EQ(loaded.polling_minutes, 60);
    EXPECT_TRUE(loaded.setup_complete);
}
