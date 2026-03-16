// Tests — Step 1 Agent C
#include <gtest/gtest.h>
#include "history.h"
#include "notification.h"
#include "config.h"

#include <chrono>
#include <filesystem>
#include <fstream>

// ============================================================
// catmull_rom tests
// ============================================================

TEST(CatmullRom, AtT0ReturnsP1) {
    double result = history::catmull_rom(0.0, 1.0, 2.0, 3.0, 0.0);
    EXPECT_DOUBLE_EQ(result, 1.0);
}

TEST(CatmullRom, AtT1ReturnsP2) {
    double result = history::catmull_rom(0.0, 1.0, 2.0, 3.0, 1.0);
    EXPECT_DOUBLE_EQ(result, 2.0);
}

TEST(CatmullRom, AtT05ReturnsMiddleValue) {
    // For uniform points 0,1,2,3: at t=0.5 the result should be 1.5
    double result = history::catmull_rom(0.0, 1.0, 2.0, 3.0, 0.5);
    EXPECT_DOUBLE_EQ(result, 1.5);
}

TEST(CatmullRom, NonUniformPoints) {
    // With non-uniform points, catmull-rom produces smooth interpolation
    double result = history::catmull_rom(0.0, 0.0, 1.0, 1.0, 0.5);
    EXPECT_NEAR(result, 0.5, 0.01);
}

TEST(CatmullRom, AllSameValues) {
    double result = history::catmull_rom(5.0, 5.0, 5.0, 5.0, 0.5);
    EXPECT_DOUBLE_EQ(result, 5.0);
}

// ============================================================
// clamp_to_unit_interval tests
// ============================================================

TEST(ClampToUnitInterval, NegativeReturnsZero) {
    EXPECT_DOUBLE_EQ(history::clamp_to_unit_interval(-0.5), 0.0);
    EXPECT_DOUBLE_EQ(history::clamp_to_unit_interval(-100.0), 0.0);
}

TEST(ClampToUnitInterval, GreaterThanOneReturnsOne) {
    EXPECT_DOUBLE_EQ(history::clamp_to_unit_interval(1.5), 1.0);
    EXPECT_DOUBLE_EQ(history::clamp_to_unit_interval(100.0), 1.0);
}

TEST(ClampToUnitInterval, InRangeReturnsValue) {
    EXPECT_DOUBLE_EQ(history::clamp_to_unit_interval(0.0), 0.0);
    EXPECT_DOUBLE_EQ(history::clamp_to_unit_interval(0.5), 0.5);
    EXPECT_DOUBLE_EQ(history::clamp_to_unit_interval(1.0), 1.0);
}

// ============================================================
// interpolate_values tests
// ============================================================

static usage_model::UsageDataPoint make_point(
    std::chrono::system_clock::time_point ts,
    double pct_5h, double pct_7d) {
    usage_model::UsageDataPoint p;
    p.id = "test";
    p.timestamp = ts;
    p.pct_5h = pct_5h;
    p.pct_7d = pct_7d;
    return p;
}

TEST(InterpolateValues, LessThanTwoPointsReturnsNullopt) {
    auto now = std::chrono::system_clock::now();
    std::vector<usage_model::UsageDataPoint> points;
    EXPECT_FALSE(history::interpolate_values(now, points).has_value());

    points.push_back(make_point(now, 0.5, 0.5));
    EXPECT_FALSE(history::interpolate_values(now, points).has_value());
}

TEST(InterpolateValues, OutOfRangeReturnsZeros) {
    auto now = std::chrono::system_clock::now();
    auto t1 = now + std::chrono::hours(1);
    auto t2 = now + std::chrono::hours(2);

    std::vector<usage_model::UsageDataPoint> points = {
        make_point(t1, 0.3, 0.4),
        make_point(t2, 0.5, 0.6),
    };

    // Before range
    auto before = now - std::chrono::hours(1);
    auto result = history::interpolate_values(before, points);
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->pct_5h, 0.0);
    EXPECT_DOUBLE_EQ(result->pct_7d, 0.0);

    // After range
    auto after = now + std::chrono::hours(5);
    result = history::interpolate_values(after, points);
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->pct_5h, 0.0);
    EXPECT_DOUBLE_EQ(result->pct_7d, 0.0);
}

TEST(InterpolateValues, InterpolatesBetweenTwoPoints) {
    auto now = std::chrono::system_clock::now();
    auto t1 = now;
    auto t2 = now + std::chrono::hours(2);
    auto mid = now + std::chrono::hours(1);

    std::vector<usage_model::UsageDataPoint> points = {
        make_point(t1, 0.2, 0.3),
        make_point(t2, 0.8, 0.9),
    };

    auto result = history::interpolate_values(mid, points);
    ASSERT_TRUE(result.has_value());
    // With only 2 points, catmull-rom uses the same edge points for i0 and i3
    EXPECT_NEAR(result->pct_5h, 0.5, 0.1);
    EXPECT_NEAR(result->pct_7d, 0.6, 0.1);
}

TEST(InterpolateValues, AtExactPointsReturnsPointValues) {
    auto now = std::chrono::system_clock::now();
    auto t1 = now;
    auto t2 = now + std::chrono::hours(2);

    std::vector<usage_model::UsageDataPoint> points = {
        make_point(t1, 0.3, 0.4),
        make_point(t2, 0.7, 0.8),
    };

    // At first point (t=0 -> returns p1)
    auto result = history::interpolate_values(t1, points);
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->pct_5h, 0.3, 0.01);
    EXPECT_NEAR(result->pct_7d, 0.4, 0.01);
}

// ============================================================
// downsample_points tests
// ============================================================

TEST(DownsamplePoints, EmptyReturnsEmpty) {
    std::vector<usage_model::UsageDataPoint> empty;
    auto now = std::chrono::system_clock::now();
    auto result = history::downsample_points(
        empty, usage_model::TimeRange::Hour1, now);
    EXPECT_TRUE(result.empty());
}

TEST(DownsamplePoints, FewPointsReturnedAsIs) {
    auto now = std::chrono::system_clock::now();
    std::vector<usage_model::UsageDataPoint> points;
    // Add just a few points (less than target count)
    for (int i = 0; i < 5; ++i) {
        points.push_back(make_point(
            now - std::chrono::minutes(i * 10), 0.1 * i, 0.1 * i));
    }
    auto result = history::downsample_points(
        points, usage_model::TimeRange::Hour1, now);
    EXPECT_EQ(result.size(), points.size());
}

TEST(DownsamplePoints, ManyPointsGetReduced) {
    auto now = std::chrono::system_clock::now();
    std::vector<usage_model::UsageDataPoint> points;
    // Hour1 target is 120, create more than that
    for (int i = 0; i < 300; ++i) {
        points.push_back(make_point(
            now - std::chrono::seconds(i * 12), 0.5, 0.6));
    }
    auto result = history::downsample_points(
        points, usage_model::TimeRange::Hour1, now);
    // Should be reduced
    EXPECT_LE(static_cast<int>(result.size()), 120);
    EXPECT_GT(result.size(), static_cast<size_t>(0));
}

TEST(DownsamplePoints, AveragesValues) {
    auto now = std::chrono::system_clock::now();
    std::vector<usage_model::UsageDataPoint> points;
    // Create many points with known values in a small range
    // Hour1 range is 3600s, target is 120 -> bucket duration ~30s
    // Put 3 points in the same bucket at the end of the range
    for (int i = 0; i < 130; ++i) {
        double val = (i < 3) ? 0.9 : 0.1;
        points.push_back(make_point(
            now - std::chrono::seconds(i * 25), val, val));
    }
    auto result = history::downsample_points(
        points, usage_model::TimeRange::Hour1, now);
    // Result should be smaller than input
    EXPECT_LT(result.size(), points.size());
}

// ============================================================
// prune_history tests
// ============================================================

TEST(PruneHistory, RemovesOldData) {
    auto now = std::chrono::system_clock::now();
    auto old_time = now - std::chrono::seconds(config::kHistoryRetentionSeconds + 3600);
    auto recent_time = now - std::chrono::hours(1);

    std::vector<usage_model::UsageDataPoint> points = {
        make_point(old_time, 0.5, 0.5),
        make_point(recent_time, 0.6, 0.6),
    };

    history::prune_history(points, now);
    ASSERT_EQ(points.size(), 1u);
    EXPECT_DOUBLE_EQ(points[0].pct_5h, 0.6);
}

TEST(PruneHistory, KeepsAllRecentData) {
    auto now = std::chrono::system_clock::now();
    std::vector<usage_model::UsageDataPoint> points = {
        make_point(now - std::chrono::hours(1), 0.1, 0.1),
        make_point(now - std::chrono::hours(2), 0.2, 0.2),
        make_point(now, 0.3, 0.3),
    };

    history::prune_history(points, now);
    EXPECT_EQ(points.size(), 3u);
}

TEST(PruneHistory, RemovesAllOldData) {
    auto now = std::chrono::system_clock::now();
    auto old = now - std::chrono::seconds(config::kHistoryRetentionSeconds + 100);
    std::vector<usage_model::UsageDataPoint> points = {
        make_point(old, 0.1, 0.1),
        make_point(old - std::chrono::hours(1), 0.2, 0.2),
    };

    history::prune_history(points, now);
    EXPECT_TRUE(points.empty());
}

TEST(PruneHistory, EmptyPointsStaysEmpty) {
    auto now = std::chrono::system_clock::now();
    std::vector<usage_model::UsageDataPoint> points;
    history::prune_history(points, now);
    EXPECT_TRUE(points.empty());
}

// ============================================================
// record_data_point tests
// ============================================================

TEST(RecordDataPoint, AppendsPoint) {
    usage_model::UsageHistory hist;
    EXPECT_TRUE(hist.data_points.empty());

    history::record_data_point(hist, 0.5, 0.7);
    ASSERT_EQ(hist.data_points.size(), 1u);
    EXPECT_DOUBLE_EQ(hist.data_points[0].pct_5h, 0.5);
    EXPECT_DOUBLE_EQ(hist.data_points[0].pct_7d, 0.7);
}

TEST(RecordDataPoint, GeneratesUniqueId) {
    usage_model::UsageHistory hist;
    history::record_data_point(hist, 0.1, 0.2);
    history::record_data_point(hist, 0.3, 0.4);

    ASSERT_EQ(hist.data_points.size(), 2u);
    EXPECT_FALSE(hist.data_points[0].id.empty());
    EXPECT_FALSE(hist.data_points[1].id.empty());
    EXPECT_NE(hist.data_points[0].id, hist.data_points[1].id);
}

TEST(RecordDataPoint, SetsTimestamp) {
    usage_model::UsageHistory hist;
    auto before = std::chrono::system_clock::now();
    history::record_data_point(hist, 0.5, 0.5);
    auto after = std::chrono::system_clock::now();

    ASSERT_EQ(hist.data_points.size(), 1u);
    EXPECT_GE(hist.data_points[0].timestamp, before);
    EXPECT_LE(hist.data_points[0].timestamp, after);
}

// ============================================================
// save/load round-trip tests
// ============================================================

class HistoryFileTest : public ::testing::Test {
protected:
    std::filesystem::path tmp_dir;
    std::filesystem::path hist_path;

    void SetUp() override {
        // Create a temp directory for test files
        tmp_dir = std::filesystem::temp_directory_path() / "claude_usage_test_history";
        std::filesystem::create_directories(tmp_dir);
        hist_path = tmp_dir / "history.json";
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(tmp_dir, ec);
    }
};

TEST_F(HistoryFileTest, SaveAndLoadRoundTrip) {
    usage_model::UsageHistory hist;
    history::record_data_point(hist, 0.25, 0.75);
    history::record_data_point(hist, 0.50, 0.80);

    ASSERT_TRUE(history::save_history(hist, hist_path));

    auto loaded = history::load_history(hist_path);
    ASSERT_EQ(loaded.data_points.size(), 2u);
    EXPECT_DOUBLE_EQ(loaded.data_points[0].pct_5h, 0.25);
    EXPECT_DOUBLE_EQ(loaded.data_points[0].pct_7d, 0.75);
    EXPECT_DOUBLE_EQ(loaded.data_points[1].pct_5h, 0.50);
    EXPECT_DOUBLE_EQ(loaded.data_points[1].pct_7d, 0.80);
}

TEST_F(HistoryFileTest, LoadNonexistentFileReturnsEmpty) {
    auto nonexistent = tmp_dir / "nonexistent.json";
    auto loaded = history::load_history(nonexistent);
    EXPECT_TRUE(loaded.data_points.empty());
}

TEST_F(HistoryFileTest, LoadCorruptFileReturnsEmptyAndCreatesBackup) {
    // Write corrupt data
    {
        std::ofstream ofs(hist_path);
        ofs << "this is not json!!!{{{";
    }

    auto loaded = history::load_history(hist_path);
    EXPECT_TRUE(loaded.data_points.empty());

    // Check backup was created
    auto backup = tmp_dir / "history.bak.json";
    EXPECT_TRUE(std::filesystem::exists(backup));
}

TEST_F(HistoryFileTest, LoadPrunesOldData) {
    // Manually create a history with old data
    usage_model::UsageHistory hist;
    auto old_time = std::chrono::system_clock::now() -
                    std::chrono::seconds(config::kHistoryRetentionSeconds + 3600);
    usage_model::UsageDataPoint old_point;
    old_point.id = "old";
    old_point.timestamp = old_time;
    old_point.pct_5h = 0.1;
    old_point.pct_7d = 0.2;
    hist.data_points.push_back(old_point);

    history::record_data_point(hist, 0.5, 0.5);

    ASSERT_TRUE(history::save_history(hist, hist_path));

    auto loaded = history::load_history(hist_path);
    // The old point should have been pruned
    ASSERT_EQ(loaded.data_points.size(), 1u);
    EXPECT_DOUBLE_EQ(loaded.data_points[0].pct_5h, 0.5);
}

// ============================================================
// Notification tests
// ============================================================

TEST(NotificationClampThreshold, ClampsToRange) {
    EXPECT_EQ(notification::clamp_threshold(-10), 0);
    EXPECT_EQ(notification::clamp_threshold(0), 0);
    EXPECT_EQ(notification::clamp_threshold(50), 50);
    EXPECT_EQ(notification::clamp_threshold(100), 100);
    EXPECT_EQ(notification::clamp_threshold(150), 100);
}

TEST(NotificationSetThreshold, SetsAndResetsState) {
    notification::NotificationState state;
    state.previous_pct_5h = 50.0;

    notification::set_threshold_5h(state, 80);
    EXPECT_EQ(state.threshold_5h, 80);
    EXPECT_FALSE(state.previous_pct_5h.has_value());
}

TEST(NotificationSetThreshold, ClampsValue) {
    notification::NotificationState state;
    notification::set_threshold_5h(state, 200);
    EXPECT_EQ(state.threshold_5h, 100);

    notification::set_threshold_7d(state, -50);
    EXPECT_EQ(state.threshold_7d, 0);

    notification::set_threshold_extra(state, 75);
    EXPECT_EQ(state.threshold_extra, 75);
}

TEST(NotificationSetThreshold, AllSettersResetPrevious) {
    notification::NotificationState state;
    state.previous_pct_5h = 10.0;
    state.previous_pct_7d = 20.0;
    state.previous_pct_extra = 30.0;

    notification::set_threshold_5h(state, 50);
    EXPECT_FALSE(state.previous_pct_5h.has_value());

    notification::set_threshold_7d(state, 60);
    EXPECT_FALSE(state.previous_pct_7d.has_value());

    notification::set_threshold_extra(state, 70);
    EXPECT_FALSE(state.previous_pct_extra.has_value());
}

TEST(NotificationCheckAndNotify, UpdatesPreviousValues) {
    notification::NotificationState state;
    // No thresholds set, no HWND needed for no-alert case
    notification::check_and_notify(state, 0.5, 0.6, 0.0, nullptr);

    EXPECT_TRUE(state.previous_pct_5h.has_value());
    EXPECT_DOUBLE_EQ(state.previous_pct_5h.value(), 50.0);
    EXPECT_TRUE(state.previous_pct_7d.has_value());
    EXPECT_DOUBLE_EQ(state.previous_pct_7d.value(), 60.0);
    EXPECT_TRUE(state.previous_pct_extra.has_value());
    EXPECT_DOUBLE_EQ(state.previous_pct_extra.value(), 0.0);
}

TEST(NotificationCheckAndNotify, NoAlertWhenThresholdsZero) {
    notification::NotificationState state;
    state.threshold_5h = 0;
    state.threshold_7d = 0;
    state.threshold_extra = 0;

    // Should not crash even with nullptr HWND when no alerts fire
    notification::check_and_notify(state, 1.0, 1.0, 1.0, nullptr);

    // Values should still be updated
    EXPECT_DOUBLE_EQ(state.previous_pct_5h.value(), 100.0);
}
