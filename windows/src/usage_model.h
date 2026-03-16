#pragma once

// Data models and pure functions for Claude API usage data.
// Ported from macOS: UsageModel.swift, UsageHistoryModel.swift, NotificationService.swift (pure part)

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace usage_model {

// ============================================================
// Data structures
// ============================================================

struct ExtraUsage {
    bool is_enabled = false;
    std::optional<double> utilization;
    std::optional<double> used_credits;     // API returns minor units (cents)
    std::optional<double> monthly_limit;    // API returns minor units (cents)
};

struct UsageBucket {
    std::optional<double> utilization;      // 0-100 percentage
    std::optional<std::string> resets_at;   // ISO 8601 date string
};

struct UsageResponse {
    std::optional<UsageBucket> five_hour;
    std::optional<UsageBucket> seven_day;
    std::optional<UsageBucket> seven_day_opus;
    std::optional<UsageBucket> seven_day_sonnet;
    std::optional<ExtraUsage> extra_usage;
};

struct UsageDataPoint {
    std::string id;
    std::chrono::system_clock::time_point timestamp;
    double pct_5h = 0.0;
    double pct_7d = 0.0;
};

struct UsageHistory {
    std::vector<UsageDataPoint> data_points;
};

enum class TimeRange {
    Hour1,
    Hour6,
    Day1,
    Day7,
    Day30
};

struct InterpolatedValues {
    std::chrono::system_clock::time_point date;
    double pct_5h = 0.0;
    double pct_7d = 0.0;
};

struct ThresholdAlert {
    std::wstring window;
    int pct = 0;
};

// ============================================================
// ExtraUsage helpers
// ============================================================

// Convert minor units (cents) to dollars
std::optional<double> used_credits_amount(const ExtraUsage& eu);
std::optional<double> monthly_limit_amount(const ExtraUsage& eu);

// Format a dollar amount as "$1.23"
std::string format_usd(double amount);

// ============================================================
// Reset date parsing & calculation
// ============================================================

// Parse ISO 8601 date string (multiple format variants)
std::optional<std::chrono::system_clock::time_point> parse_reset_date(
    const std::string& value);

// Get parsed reset date from a bucket
std::optional<std::chrono::system_clock::time_point> resets_at_date(
    const UsageBucket& bucket);

// Calculate next reset date by stepping forward from a previous date
std::chrono::system_clock::time_point next_reset_date(
    std::chrono::system_clock::time_point previous,
    std::chrono::seconds reset_interval,
    std::chrono::system_clock::time_point now);

// Format a time_point as ISO 8601 string
std::string format_reset_date(std::chrono::system_clock::time_point date);

// ============================================================
// Reconciliation
// ============================================================

// Fill in missing resets_at using previous known value
UsageBucket reconcile_bucket(
    const UsageBucket& current,
    const std::optional<UsageBucket>& previous,
    std::chrono::seconds reset_interval,
    std::chrono::system_clock::time_point now);

// Reconcile all buckets in a response
UsageResponse reconcile_response(
    const UsageResponse& current,
    const std::optional<UsageResponse>& previous,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

// ============================================================
// Backoff
// ============================================================

// Calculate next polling interval after rate limiting (429)
double backoff_interval(
    std::optional<double> retry_after,
    double current_interval);

// ============================================================
// Polling option helpers
// ============================================================

bool is_discouraged_polling_option(int minutes);
std::wstring polling_option_label(int minutes);

// ============================================================
// Notification threshold logic (pure)
// ============================================================

// Returns alerts for thresholds that were just crossed (previous < threshold <= current)
std::vector<ThresholdAlert> crossed_thresholds(
    int threshold_5h,
    int threshold_7d,
    int threshold_extra,
    double previous_5h,
    double previous_7d,
    double previous_extra,
    double current_5h,
    double current_7d,
    double current_extra);

// ============================================================
// TimeRange helpers
// ============================================================

std::chrono::seconds time_range_interval(TimeRange range);
int time_range_target_point_count(TimeRange range);
const char* time_range_label(TimeRange range);

// ============================================================
// JSON serialization (nlohmann::json ADL)
// ============================================================

void from_json(const nlohmann::json& j, ExtraUsage& v);
void to_json(nlohmann::json& j, const ExtraUsage& v);

void from_json(const nlohmann::json& j, UsageBucket& v);
void to_json(nlohmann::json& j, const UsageBucket& v);

void from_json(const nlohmann::json& j, UsageResponse& v);
void to_json(nlohmann::json& j, const UsageResponse& v);

void from_json(const nlohmann::json& j, UsageDataPoint& v);
void to_json(nlohmann::json& j, const UsageDataPoint& v);

void from_json(const nlohmann::json& j, UsageHistory& v);
void to_json(nlohmann::json& j, const UsageHistory& v);

}  // namespace usage_model
