// Tests — Step 1 Agent A
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cmath>
#include <string>

#include "usage_model.h"
#include "config.h"

using namespace usage_model;
using json = nlohmann::json;
using time_point = std::chrono::system_clock::time_point;

// ============================================================
// Test helpers
// ============================================================

static time_point make_date(const std::string& iso) {
    auto parsed = parse_reset_date(iso);
    EXPECT_TRUE(parsed.has_value()) << "Failed to parse date: " << iso;
    return parsed.value_or(std::chrono::system_clock::time_point{});
}

static std::string iso_string(time_point tp) {
    return format_reset_date(tp);
}

static UsageResponse make_response(
    std::optional<UsageBucket> five_hour = std::nullopt,
    std::optional<UsageBucket> seven_day = std::nullopt,
    std::optional<UsageBucket> seven_day_opus = std::nullopt,
    std::optional<UsageBucket> seven_day_sonnet = std::nullopt,
    std::optional<ExtraUsage> extra_usage = std::nullopt) {
    UsageResponse r;
    r.five_hour = five_hour;
    r.seven_day = seven_day;
    r.seven_day_opus = seven_day_opus;
    r.seven_day_sonnet = seven_day_sonnet;
    r.extra_usage = extra_usage;
    return r;
}

static UsageBucket make_bucket(
    std::optional<double> utilization = std::nullopt,
    std::optional<std::string> resets_at = std::nullopt) {
    UsageBucket b;
    b.utilization = utilization;
    b.resets_at = resets_at;
    return b;
}

// ============================================================
// parse_reset_date tests
// ============================================================

TEST(ParseResetDate, ParsesIso8601WithZ) {
    auto result = parse_reset_date("2026-03-05T18:00:00Z");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(format_reset_date(result.value()), "2026-03-05T18:00:00Z");
}

TEST(ParseResetDate, ParsesIso8601WithFractionalSecondsAndZ) {
    auto result = parse_reset_date("2026-03-05T18:00:00.000Z");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(format_reset_date(result.value()), "2026-03-05T18:00:00Z");
}

TEST(ParseResetDate, ParsesIso8601WithMicroseconds) {
    auto result = parse_reset_date("2026-03-05T18:00:00.123456Z");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(format_reset_date(result.value()), "2026-03-05T18:00:00Z");
}

TEST(ParseResetDate, ParsesTimestampWithoutTimezoneAsUTC) {
    // From macOS test: testResetDateParsesTimestampWithoutTimezoneAsUTC
    auto result = parse_reset_date("2026-03-05T18:00:00");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(format_reset_date(result.value()), "2026-03-05T18:00:00Z");
}

TEST(ParseResetDate, ParsesFractionalSecondsWithoutZ) {
    auto result = parse_reset_date("2026-03-05T18:00:00.123456");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(format_reset_date(result.value()), "2026-03-05T18:00:00Z");
}

TEST(ParseResetDate, ParsesMillisecondsWithoutZ) {
    auto result = parse_reset_date("2026-03-05T18:00:00.000");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(format_reset_date(result.value()), "2026-03-05T18:00:00Z");
}

TEST(ParseResetDate, ReturnsNulloptForEmptyString) {
    EXPECT_FALSE(parse_reset_date("").has_value());
}

TEST(ParseResetDate, ReturnsNulloptForInvalidString) {
    EXPECT_FALSE(parse_reset_date("not-a-date").has_value());
}

TEST(ParseResetDate, ReturnsNulloptForGarbage) {
    EXPECT_FALSE(parse_reset_date("xyz123").has_value());
}

// ============================================================
// resets_at_date tests
// ============================================================

TEST(ResetsAtDate, ReturnsParsedDateWhenPresent) {
    auto bucket = make_bucket(25.0, "2026-03-05T18:00:00Z");
    auto result = resets_at_date(bucket);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(format_reset_date(result.value()), "2026-03-05T18:00:00Z");
}

TEST(ResetsAtDate, ReturnsNulloptWhenMissing) {
    auto bucket = make_bucket(25.0, std::nullopt);
    EXPECT_FALSE(resets_at_date(bucket).has_value());
}

TEST(ResetsAtDate, ReturnsNulloptForInvalidDate) {
    auto bucket = make_bucket(25.0, "not-a-date");
    EXPECT_FALSE(resets_at_date(bucket).has_value());
}

// ============================================================
// next_reset_date tests
// ============================================================

TEST(NextResetDate, StepsForwardByResetInterval) {
    auto prev = make_date("2026-03-05T18:00:00Z");
    auto now = make_date("2026-03-05T18:05:00Z");
    auto interval = std::chrono::seconds(5 * 60 * 60);  // 5 hours

    auto result = next_reset_date(prev, interval, now);
    EXPECT_EQ(format_reset_date(result), "2026-03-05T23:00:00Z");
}

TEST(NextResetDate, StepsForwardMultipleTimes) {
    auto prev = make_date("2026-03-05T00:00:00Z");
    auto now = make_date("2026-03-05T11:00:00Z");
    auto interval = std::chrono::seconds(5 * 60 * 60);  // 5 hours

    // elapsed=11h, stepCount = floor(11/5)+1 = 3
    auto result = next_reset_date(prev, interval, now);
    // 0:00 + 3*5h = 15:00
    EXPECT_EQ(format_reset_date(result), "2026-03-05T15:00:00Z");
}

TEST(NextResetDate, ReturnsUnchangedWhenIntervalIsZero) {
    auto prev = make_date("2026-03-05T18:00:00Z");
    auto now = make_date("2026-03-05T20:00:00Z");
    auto interval = std::chrono::seconds(0);

    auto result = next_reset_date(prev, interval, now);
    EXPECT_EQ(format_reset_date(result), "2026-03-05T18:00:00Z");
}

TEST(NextResetDate, ReturnsUnchangedWhenPreviousIsInFuture) {
    auto prev = make_date("2026-03-05T22:00:00Z");
    auto now = make_date("2026-03-05T18:00:00Z");
    auto interval = std::chrono::seconds(5 * 60 * 60);

    auto result = next_reset_date(prev, interval, now);
    EXPECT_EQ(format_reset_date(result), "2026-03-05T22:00:00Z");
}

// ============================================================
// reconcile_bucket tests
// ============================================================

TEST(ReconcileBucket, KeepsCurrentWhenResetsAtIsValid) {
    // From macOS test: testReconcilePreservesValidServerReset
    auto current = make_bucket(2.0, "2026-03-05T22:00:00Z");
    auto previous = make_bucket(100.0, "2026-03-05T18:00:00Z");
    auto now = make_date("2026-03-05T18:05:00Z");
    auto interval = std::chrono::seconds(5 * 60 * 60);

    auto result = reconcile_bucket(current, previous, interval, now);
    ASSERT_TRUE(result.resets_at.has_value());
    EXPECT_EQ(result.resets_at.value(), "2026-03-05T22:00:00Z");
}

TEST(ReconcileBucket, EstimatesFromPreviousWhenResetsAtIsMissing) {
    // From macOS test: testReconcileKeepsPreviousResetWhenServerTemporarilyDropsIt
    auto current = make_bucket(89.0, std::nullopt);
    auto previous = make_bucket(88.0, "2026-03-05T18:00:00Z");
    auto now = make_date("2026-03-05T17:30:00Z");
    auto interval = std::chrono::seconds(5 * 60 * 60);

    auto result = reconcile_bucket(current, previous, interval, now);
    auto date = resets_at_date(result);
    ASSERT_TRUE(date.has_value());
    // previous (18:00) is in the future relative to now (17:30), so it stays
    EXPECT_EQ(format_reset_date(date.value()), "2026-03-05T18:00:00Z");
}

TEST(ReconcileBucket, AdvancesResetAfterRollover) {
    // From macOS test: testReconcileAdvancesResetAfterRolloverWhenServerDropsIt
    auto current = make_bucket(2.0, "not-a-date");
    auto previous = make_bucket(100.0, "2026-03-05T18:00:00Z");
    auto now = make_date("2026-03-05T18:05:00Z");
    auto interval = std::chrono::seconds(5 * 60 * 60);

    auto result = reconcile_bucket(current, previous, interval, now);
    auto date = resets_at_date(result);
    ASSERT_TRUE(date.has_value());
    EXPECT_EQ(format_reset_date(date.value()), "2026-03-05T23:00:00Z");
}

TEST(ReconcileBucket, ReturnCurrentWhenNoPrevious) {
    auto current = make_bucket(50.0, std::nullopt);
    auto now = make_date("2026-03-05T18:00:00Z");
    auto interval = std::chrono::seconds(5 * 60 * 60);

    auto result = reconcile_bucket(current, std::nullopt, interval, now);
    EXPECT_FALSE(result.resets_at.has_value());
    EXPECT_EQ(result.utilization.value_or(0.0), 50.0);
}

// ============================================================
// reconcile_response tests
// ============================================================

TEST(ReconcileResponse, ReconcilesFiveHourBucket) {
    auto previous_reset = make_date("2026-03-05T18:00:00Z");
    auto previous = make_response(
        make_bucket(88.0, iso_string(previous_reset)));
    auto current = make_response(
        make_bucket(89.0, std::nullopt));
    auto now = make_date("2026-03-05T17:30:00Z");

    auto result = reconcile_response(current, previous, now);
    ASSERT_TRUE(result.five_hour.has_value());
    auto date = resets_at_date(result.five_hour.value());
    ASSERT_TRUE(date.has_value());
    EXPECT_EQ(format_reset_date(date.value()), "2026-03-05T18:00:00Z");
}

TEST(ReconcileResponse, PreservesExtraUsage) {
    ExtraUsage eu;
    eu.is_enabled = true;
    eu.utilization = 50.0;
    eu.used_credits = 1500;
    eu.monthly_limit = 10000;

    auto current = make_response(std::nullopt, std::nullopt, std::nullopt, std::nullopt, eu);
    auto now = make_date("2026-03-05T18:00:00Z");

    auto result = reconcile_response(current, std::nullopt, now);
    ASSERT_TRUE(result.extra_usage.has_value());
    EXPECT_TRUE(result.extra_usage.value().is_enabled);
    EXPECT_DOUBLE_EQ(result.extra_usage.value().used_credits.value(), 1500.0);
}

// ============================================================
// backoff_interval tests
// ============================================================

TEST(BackoffInterval, DoublesCurrentWhenNoRetryAfter) {
    auto result = backoff_interval(std::nullopt, 30.0);
    EXPECT_DOUBLE_EQ(result, 60.0);  // max(30, 30*2) = 60
}

TEST(BackoffInterval, UsesRetryAfterWhenLarger) {
    auto result = backoff_interval(120.0, 30.0);
    EXPECT_DOUBLE_EQ(result, 120.0);  // max(120, 60) = 120
}

TEST(BackoffInterval, UsesDoubledWhenLargerThanRetryAfter) {
    auto result = backoff_interval(50.0, 100.0);
    EXPECT_DOUBLE_EQ(result, 200.0);  // max(50, 200) = 200
}

TEST(BackoffInterval, CapsAtMaxBackoffInterval) {
    auto result = backoff_interval(std::nullopt, 2000.0);
    EXPECT_DOUBLE_EQ(result, config::kMaxBackoffInterval);
}

TEST(BackoffInterval, RetryAfterExceedsMaxIsCapped) {
    auto result = backoff_interval(5000.0, 100.0);
    EXPECT_DOUBLE_EQ(result, config::kMaxBackoffInterval);
}

// ============================================================
// format_usd tests
// ============================================================

TEST(FormatUsd, FormatsPositiveAmount) {
    EXPECT_EQ(format_usd(1.23), "$1.23");
}

TEST(FormatUsd, FormatsZero) {
    EXPECT_EQ(format_usd(0.0), "$0.00");
}

TEST(FormatUsd, FormatsWholeNumber) {
    EXPECT_EQ(format_usd(100.0), "$100.00");
}

TEST(FormatUsd, RoundsTwoDecimalPlaces) {
    EXPECT_EQ(format_usd(1.999), "$2.00");
}

TEST(FormatUsd, FormatsSmallAmount) {
    EXPECT_EQ(format_usd(0.01), "$0.01");
}

// ============================================================
// ExtraUsage helpers tests
// ============================================================

TEST(ExtraUsageHelpers, UsedCreditsAmountConvertsCentsToDollars) {
    ExtraUsage eu;
    eu.used_credits = 1500.0;
    auto result = used_credits_amount(eu);
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result.value(), 15.0);
}

TEST(ExtraUsageHelpers, UsedCreditsAmountReturnsNulloptWhenMissing) {
    ExtraUsage eu;
    EXPECT_FALSE(used_credits_amount(eu).has_value());
}

TEST(ExtraUsageHelpers, MonthlyLimitAmountConvertsCentsToDollars) {
    ExtraUsage eu;
    eu.monthly_limit = 10000.0;
    auto result = monthly_limit_amount(eu);
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result.value(), 100.0);
}

TEST(ExtraUsageHelpers, MonthlyLimitAmountReturnsNulloptWhenMissing) {
    ExtraUsage eu;
    EXPECT_FALSE(monthly_limit_amount(eu).has_value());
}

// ============================================================
// crossed_thresholds tests
// ============================================================

TEST(CrossedThresholds, ReturnsAlertWhenCrossed) {
    auto alerts = crossed_thresholds(
        80, 0, 0,    // thresholds
        70.0, 0.0, 0.0,  // previous
        85.0, 0.0, 0.0   // current
    );
    ASSERT_EQ(alerts.size(), 1u);
    EXPECT_EQ(alerts[0].window, L"5-hour");
    EXPECT_EQ(alerts[0].pct, 85);
}

TEST(CrossedThresholds, NoAlertWhenBelowThreshold) {
    auto alerts = crossed_thresholds(
        80, 0, 0,
        70.0, 0.0, 0.0,
        75.0, 0.0, 0.0
    );
    EXPECT_TRUE(alerts.empty());
}

TEST(CrossedThresholds, NoAlertWhenAlreadyAboveThreshold) {
    auto alerts = crossed_thresholds(
        80, 0, 0,
        85.0, 0.0, 0.0,
        90.0, 0.0, 0.0
    );
    EXPECT_TRUE(alerts.empty());
}

TEST(CrossedThresholds, ThresholdZeroMeansDisabled) {
    auto alerts = crossed_thresholds(
        0, 0, 0,
        0.0, 0.0, 0.0,
        100.0, 100.0, 100.0
    );
    EXPECT_TRUE(alerts.empty());
}

TEST(CrossedThresholds, AllThreeCanFireSimultaneously) {
    auto alerts = crossed_thresholds(
        50, 60, 70,
        40.0, 50.0, 60.0,
        55.0, 65.0, 75.0
    );
    ASSERT_EQ(alerts.size(), 3u);
    EXPECT_EQ(alerts[0].window, L"5-hour");
    EXPECT_EQ(alerts[1].window, L"7-day");
    EXPECT_EQ(alerts[2].window, L"Extra usage");
}

TEST(CrossedThresholds, SevenDayAlertWhenCrossed) {
    auto alerts = crossed_thresholds(
        0, 90, 0,
        80.0, 85.0, 0.0,
        80.0, 95.0, 0.0
    );
    ASSERT_EQ(alerts.size(), 1u);
    EXPECT_EQ(alerts[0].window, L"7-day");
    EXPECT_EQ(alerts[0].pct, 95);
}

TEST(CrossedThresholds, ExtraUsageAlertWhenCrossed) {
    auto alerts = crossed_thresholds(
        0, 0, 50,
        0.0, 0.0, 40.0,
        0.0, 0.0, 55.0
    );
    ASSERT_EQ(alerts.size(), 1u);
    EXPECT_EQ(alerts[0].window, L"Extra usage");
    EXPECT_EQ(alerts[0].pct, 55);
}

TEST(CrossedThresholds, RoundsCurrentValue) {
    auto alerts = crossed_thresholds(
        80, 0, 0,
        70.0, 0.0, 0.0,
        80.6, 0.0, 0.0
    );
    ASSERT_EQ(alerts.size(), 1u);
    EXPECT_EQ(alerts[0].pct, 81);
}

// ============================================================
// TimeRange helpers tests
// ============================================================

TEST(TimeRange, IntervalValues) {
    EXPECT_EQ(time_range_interval(TimeRange::Hour1).count(), 3600);
    EXPECT_EQ(time_range_interval(TimeRange::Hour6).count(), 6 * 3600);
    EXPECT_EQ(time_range_interval(TimeRange::Day1).count(), 86400);
    EXPECT_EQ(time_range_interval(TimeRange::Day7).count(), 7 * 86400);
    EXPECT_EQ(time_range_interval(TimeRange::Day30).count(), 30 * 86400);
}

TEST(TimeRange, TargetPointCount) {
    EXPECT_EQ(time_range_target_point_count(TimeRange::Hour1), 120);
    EXPECT_EQ(time_range_target_point_count(TimeRange::Hour6), 180);
    EXPECT_EQ(time_range_target_point_count(TimeRange::Day1), 200);
    EXPECT_EQ(time_range_target_point_count(TimeRange::Day7), 200);
    EXPECT_EQ(time_range_target_point_count(TimeRange::Day30), 200);
}

TEST(TimeRange, Labels) {
    EXPECT_STREQ(time_range_label(TimeRange::Hour1), "1h");
    EXPECT_STREQ(time_range_label(TimeRange::Hour6), "6h");
    EXPECT_STREQ(time_range_label(TimeRange::Day1), "1d");
    EXPECT_STREQ(time_range_label(TimeRange::Day7), "7d");
    EXPECT_STREQ(time_range_label(TimeRange::Day30), "30d");
}

// ============================================================
// polling_option_label & is_discouraged_polling_option tests
// ============================================================

TEST(PollingOption, IsDiscouragedFor5And15) {
    EXPECT_TRUE(is_discouraged_polling_option(5));
    EXPECT_TRUE(is_discouraged_polling_option(15));
    EXPECT_FALSE(is_discouraged_polling_option(30));
    EXPECT_FALSE(is_discouraged_polling_option(60));
}

TEST(PollingOption, LabelForMinutes) {
    EXPECT_EQ(polling_option_label(5), L"5min (not recommended)");
    EXPECT_EQ(polling_option_label(15), L"15min (not recommended)");
    EXPECT_EQ(polling_option_label(30), L"30min");
    EXPECT_EQ(polling_option_label(60), L"1h");
}

// ============================================================
// JSON roundtrip tests
// ============================================================

TEST(JsonRoundtrip, ExtraUsage) {
    ExtraUsage eu;
    eu.is_enabled = true;
    eu.utilization = 42.5;
    eu.used_credits = 1500.0;
    eu.monthly_limit = 10000.0;

    json j = eu;
    auto eu2 = j.get<ExtraUsage>();
    EXPECT_EQ(eu2.is_enabled, eu.is_enabled);
    EXPECT_DOUBLE_EQ(eu2.utilization.value(), eu.utilization.value());
    EXPECT_DOUBLE_EQ(eu2.used_credits.value(), eu.used_credits.value());
    EXPECT_DOUBLE_EQ(eu2.monthly_limit.value(), eu.monthly_limit.value());
}

TEST(JsonRoundtrip, ExtraUsageWithNulls) {
    ExtraUsage eu;
    eu.is_enabled = false;

    json j = eu;
    EXPECT_TRUE(j["utilization"].is_null());
    EXPECT_TRUE(j["used_credits"].is_null());
    EXPECT_TRUE(j["monthly_limit"].is_null());

    auto eu2 = j.get<ExtraUsage>();
    EXPECT_FALSE(eu2.is_enabled);
    EXPECT_FALSE(eu2.utilization.has_value());
    EXPECT_FALSE(eu2.used_credits.has_value());
    EXPECT_FALSE(eu2.monthly_limit.has_value());
}

TEST(JsonRoundtrip, UsageBucket) {
    auto bucket = make_bucket(75.5, "2026-03-05T18:00:00Z");
    json j = bucket;
    auto bucket2 = j.get<UsageBucket>();
    EXPECT_DOUBLE_EQ(bucket2.utilization.value(), 75.5);
    EXPECT_EQ(bucket2.resets_at.value(), "2026-03-05T18:00:00Z");
}

TEST(JsonRoundtrip, UsageBucketWithNulls) {
    UsageBucket bucket;
    json j = bucket;
    EXPECT_TRUE(j["utilization"].is_null());
    EXPECT_TRUE(j["resets_at"].is_null());

    auto bucket2 = j.get<UsageBucket>();
    EXPECT_FALSE(bucket2.utilization.has_value());
    EXPECT_FALSE(bucket2.resets_at.has_value());
}

TEST(JsonRoundtrip, UsageResponse) {
    auto resp = make_response(
        make_bucket(50.0, "2026-03-05T18:00:00Z"),
        make_bucket(30.0, "2026-03-10T00:00:00Z"),
        make_bucket(20.0, "2026-03-10T00:00:00Z"),
        make_bucket(10.0, "2026-03-10T00:00:00Z")
    );

    json j = resp;
    auto resp2 = j.get<UsageResponse>();
    ASSERT_TRUE(resp2.five_hour.has_value());
    EXPECT_DOUBLE_EQ(resp2.five_hour.value().utilization.value(), 50.0);
    ASSERT_TRUE(resp2.seven_day.has_value());
    EXPECT_DOUBLE_EQ(resp2.seven_day.value().utilization.value(), 30.0);
    ASSERT_TRUE(resp2.seven_day_opus.has_value());
    EXPECT_DOUBLE_EQ(resp2.seven_day_opus.value().utilization.value(), 20.0);
    ASSERT_TRUE(resp2.seven_day_sonnet.has_value());
    EXPECT_DOUBLE_EQ(resp2.seven_day_sonnet.value().utilization.value(), 10.0);
}

TEST(JsonRoundtrip, UsageResponseFromApiJson) {
    auto api_json = R"({
        "five_hour": {"utilization": 88.5, "resets_at": "2026-03-05T18:00:00Z"},
        "seven_day": {"utilization": 42.0, "resets_at": null},
        "seven_day_opus": null,
        "seven_day_sonnet": null,
        "extra_usage": {"is_enabled": true, "utilization": 50.0, "used_credits": 1500, "monthly_limit": 10000}
    })"_json;

    auto resp = api_json.get<UsageResponse>();
    ASSERT_TRUE(resp.five_hour.has_value());
    EXPECT_DOUBLE_EQ(resp.five_hour.value().utilization.value(), 88.5);
    ASSERT_TRUE(resp.seven_day.has_value());
    EXPECT_DOUBLE_EQ(resp.seven_day.value().utilization.value(), 42.0);
    EXPECT_FALSE(resp.seven_day.value().resets_at.has_value());
    EXPECT_FALSE(resp.seven_day_opus.has_value());
    EXPECT_FALSE(resp.seven_day_sonnet.has_value());
    ASSERT_TRUE(resp.extra_usage.has_value());
    EXPECT_TRUE(resp.extra_usage.value().is_enabled);
}

TEST(JsonRoundtrip, UsageDataPoint) {
    UsageDataPoint dp;
    dp.id = "test-id-123";
    dp.timestamp = make_date("2026-03-05T18:00:00Z");
    dp.pct_5h = 50.0;
    dp.pct_7d = 30.0;

    json j = dp;
    EXPECT_EQ(j["id"], "test-id-123");
    EXPECT_EQ(j["timestamp"], "2026-03-05T18:00:00Z");

    auto dp2 = j.get<UsageDataPoint>();
    EXPECT_EQ(dp2.id, "test-id-123");
    EXPECT_EQ(format_reset_date(dp2.timestamp), "2026-03-05T18:00:00Z");
    EXPECT_DOUBLE_EQ(dp2.pct_5h, 50.0);
    EXPECT_DOUBLE_EQ(dp2.pct_7d, 30.0);
}

TEST(JsonRoundtrip, UsageHistory) {
    UsageHistory history;

    UsageDataPoint dp1;
    dp1.id = "id-1";
    dp1.timestamp = make_date("2026-03-05T12:00:00Z");
    dp1.pct_5h = 10.0;
    dp1.pct_7d = 20.0;

    UsageDataPoint dp2;
    dp2.id = "id-2";
    dp2.timestamp = make_date("2026-03-05T13:00:00Z");
    dp2.pct_5h = 30.0;
    dp2.pct_7d = 40.0;

    history.data_points = {dp1, dp2};

    json j = history;
    EXPECT_TRUE(j.contains("data_points"));
    EXPECT_EQ(j["data_points"].size(), 2u);

    auto history2 = j.get<UsageHistory>();
    ASSERT_EQ(history2.data_points.size(), 2u);
    EXPECT_EQ(history2.data_points[0].id, "id-1");
    EXPECT_EQ(history2.data_points[1].id, "id-2");
    EXPECT_DOUBLE_EQ(history2.data_points[0].pct_5h, 10.0);
    EXPECT_DOUBLE_EQ(history2.data_points[1].pct_7d, 40.0);
}

TEST(JsonRoundtrip, UsageHistoryEmpty) {
    UsageHistory history;
    json j = history;
    auto history2 = j.get<UsageHistory>();
    EXPECT_TRUE(history2.data_points.empty());
}
