// Implementation — Step 1 Agent A
#include "usage_model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "config.h"
#include "string_utils.h"

namespace usage_model {

// ============================================================
// ExtraUsage helpers
// ============================================================

std::optional<double> used_credits_amount(const ExtraUsage& eu) {
    if (!eu.used_credits.has_value()) return std::nullopt;
    return eu.used_credits.value() / 100.0;
}

std::optional<double> monthly_limit_amount(const ExtraUsage& eu) {
    if (!eu.monthly_limit.has_value()) return std::nullopt;
    return eu.monthly_limit.value() / 100.0;
}

std::string format_usd(double amount) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "$%.2f", amount);
    return std::string(buf);
}

// ============================================================
// Reset date parsing & calculation
// ============================================================

// Helper: try to parse with std::get_time using a given format string.
// Returns time_point if successful.
static std::optional<std::chrono::system_clock::time_point> try_parse(
    const std::string& value, const char* fmt) {
    std::tm tm_val = {};
    std::istringstream iss(value);
    iss >> std::get_time(&tm_val, fmt);
    if (iss.fail()) return std::nullopt;

    // _mkgmtime interprets tm as UTC
    tm_val.tm_isdst = 0;
    auto time_c = _mkgmtime(&tm_val);
    if (time_c == -1) return std::nullopt;

    return std::chrono::system_clock::from_time_t(time_c);
}

std::optional<std::chrono::system_clock::time_point> parse_reset_date(
    const std::string& value) {
    if (value.empty()) return std::nullopt;

    // Try ISO 8601 with Z suffix: "2024-01-01T00:00:00Z"
    // Also handles "2024-01-01T00:00:00.000Z" and "2024-01-01T00:00:00.123456Z"
    // We strip fractional seconds and Z, then parse the base.

    std::string base = value;

    // Strip trailing 'Z' if present
    bool has_z = (!base.empty() && base.back() == 'Z');
    if (has_z) {
        base.pop_back();
    }

    // Strip fractional seconds (.NNN or .NNNNNN) if present
    auto dot_pos = base.find('.');
    if (dot_pos != std::string::npos) {
        // Ensure everything after the dot (before we removed Z) is digits
        auto frac = base.substr(dot_pos + 1);
        bool all_digits = !frac.empty() && std::all_of(frac.begin(), frac.end(),
            [](char c) { return c >= '0' && c <= '9'; });
        if (all_digits) {
            base = base.substr(0, dot_pos);
        }
    }

    // Now try to parse "yyyy-MM-ddTHH:mm:ss"
    auto result = try_parse(base, "%Y-%m-%dT%H:%M:%S");
    if (result.has_value()) return result;

    return std::nullopt;
}

std::optional<std::chrono::system_clock::time_point> resets_at_date(
    const UsageBucket& bucket) {
    if (!bucket.resets_at.has_value()) return std::nullopt;
    return parse_reset_date(bucket.resets_at.value());
}

std::chrono::system_clock::time_point next_reset_date(
    std::chrono::system_clock::time_point previous,
    std::chrono::seconds reset_interval,
    std::chrono::system_clock::time_point now) {
    if (reset_interval.count() <= 0) return previous;
    if (previous > now) return previous;

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - previous);
    auto step_count = static_cast<int64_t>(
        std::floor(static_cast<double>(elapsed.count()) /
                   static_cast<double>(reset_interval.count()))) + 1;
    return previous + reset_interval * step_count;
}

std::string format_reset_date(std::chrono::system_clock::time_point date) {
    auto time_c = std::chrono::system_clock::to_time_t(date);
    std::tm tm_val = {};
    gmtime_s(&tm_val, &time_c);

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_val);
    return std::string(buf);
}

// ============================================================
// Reconciliation
// ============================================================

UsageBucket reconcile_bucket(
    const UsageBucket& current,
    const std::optional<UsageBucket>& previous,
    std::chrono::seconds reset_interval,
    std::chrono::system_clock::time_point now) {
    // If current already has a valid resets_at date, keep it
    if (resets_at_date(current).has_value()) return current;

    // If no previous bucket or no valid previous date, return current as-is
    if (!previous.has_value()) return current;
    auto prev_date = resets_at_date(previous.value());
    if (!prev_date.has_value()) return current;

    auto resolved = next_reset_date(prev_date.value(), reset_interval, now);

    UsageBucket result;
    result.utilization = current.utilization;
    result.resets_at = format_reset_date(resolved);
    return result;
}

UsageResponse reconcile_response(
    const UsageResponse& current,
    const std::optional<UsageResponse>& previous,
    std::chrono::system_clock::time_point now) {
    UsageResponse result;

    constexpr auto five_h = std::chrono::seconds(config::kFiveHourResetInterval);
    constexpr auto seven_d = std::chrono::seconds(config::kSevenDayResetInterval);

    if (current.five_hour.has_value()) {
        result.five_hour = reconcile_bucket(
            current.five_hour.value(),
            previous.has_value() ? previous.value().five_hour : std::nullopt,
            five_h, now);
    }
    if (current.seven_day.has_value()) {
        result.seven_day = reconcile_bucket(
            current.seven_day.value(),
            previous.has_value() ? previous.value().seven_day : std::nullopt,
            seven_d, now);
    }
    if (current.seven_day_opus.has_value()) {
        result.seven_day_opus = reconcile_bucket(
            current.seven_day_opus.value(),
            previous.has_value() ? previous.value().seven_day_opus : std::nullopt,
            seven_d, now);
    }
    if (current.seven_day_sonnet.has_value()) {
        result.seven_day_sonnet = reconcile_bucket(
            current.seven_day_sonnet.value(),
            previous.has_value() ? previous.value().seven_day_sonnet : std::nullopt,
            seven_d, now);
    }

    result.extra_usage = current.extra_usage;
    return result;
}

// ============================================================
// Backoff
// ============================================================

double backoff_interval(
    std::optional<double> retry_after,
    double current_interval) {
    double base = retry_after.value_or(current_interval);
    double doubled = current_interval * 2.0;
    double chosen = (std::max)(base, doubled);
    return (std::min)(chosen, config::kMaxBackoffInterval);
}

// ============================================================
// Polling option helpers
// ============================================================

bool is_discouraged_polling_option(int minutes) {
    return minutes == 5 || minutes == 15;
}

std::wstring polling_option_label(int minutes) {
    std::wstring interval;
    if (minutes < 60) {
        interval = std::to_wstring(minutes) + L"min";
    } else {
        interval = std::to_wstring(minutes / 60) + L"h";
    }

    if (is_discouraged_polling_option(minutes)) {
        return interval + L" (not recommended)";
    }
    return interval;
}

// ============================================================
// Notification threshold logic
// ============================================================

std::vector<ThresholdAlert> crossed_thresholds(
    int threshold_5h,
    int threshold_7d,
    int threshold_extra,
    double previous_5h,
    double previous_7d,
    double previous_extra,
    double current_5h,
    double current_7d,
    double current_extra) {
    std::vector<ThresholdAlert> alerts;

    if (threshold_5h > 0) {
        auto t = static_cast<double>(threshold_5h);
        if (current_5h >= t && previous_5h < t) {
            alerts.push_back({L"5-hour", static_cast<int>(std::round(current_5h))});
        }
    }

    if (threshold_7d > 0) {
        auto t = static_cast<double>(threshold_7d);
        if (current_7d >= t && previous_7d < t) {
            alerts.push_back({L"7-day", static_cast<int>(std::round(current_7d))});
        }
    }

    if (threshold_extra > 0) {
        auto t = static_cast<double>(threshold_extra);
        if (current_extra >= t && previous_extra < t) {
            alerts.push_back({L"Extra usage", static_cast<int>(std::round(current_extra))});
        }
    }

    return alerts;
}

// ============================================================
// TimeRange helpers
// ============================================================

std::chrono::seconds time_range_interval(TimeRange range) {
    switch (range) {
        case TimeRange::Hour1:  return std::chrono::seconds(3600);
        case TimeRange::Hour6:  return std::chrono::seconds(6 * 3600);
        case TimeRange::Day1:   return std::chrono::seconds(86400);
        case TimeRange::Day7:   return std::chrono::seconds(7 * 86400);
        case TimeRange::Day30:  return std::chrono::seconds(30 * 86400);
    }
    return std::chrono::seconds(3600);  // unreachable
}

int time_range_target_point_count(TimeRange range) {
    switch (range) {
        case TimeRange::Hour1:  return 120;
        case TimeRange::Hour6:  return 180;
        case TimeRange::Day1:   return 200;
        case TimeRange::Day7:   return 200;
        case TimeRange::Day30:  return 200;
    }
    return 120;  // unreachable
}

const char* time_range_label(TimeRange range) {
    switch (range) {
        case TimeRange::Hour1:  return "1h";
        case TimeRange::Hour6:  return "6h";
        case TimeRange::Day1:   return "1d";
        case TimeRange::Day7:   return "7d";
        case TimeRange::Day30:  return "30d";
    }
    return "1h";  // unreachable
}

// ============================================================
// JSON serialization
// ============================================================

void from_json(const nlohmann::json& j, ExtraUsage& v) {
    v.is_enabled = j.value("is_enabled", false);
    if (j.contains("utilization") && !j["utilization"].is_null()) {
        v.utilization = j["utilization"].get<double>();
    }
    if (j.contains("used_credits") && !j["used_credits"].is_null()) {
        v.used_credits = j["used_credits"].get<double>();
    }
    if (j.contains("monthly_limit") && !j["monthly_limit"].is_null()) {
        v.monthly_limit = j["monthly_limit"].get<double>();
    }
}

void to_json(nlohmann::json& j, const ExtraUsage& v) {
    j = nlohmann::json{{"is_enabled", v.is_enabled}};
    if (v.utilization.has_value()) {
        j["utilization"] = v.utilization.value();
    } else {
        j["utilization"] = nullptr;
    }
    if (v.used_credits.has_value()) {
        j["used_credits"] = v.used_credits.value();
    } else {
        j["used_credits"] = nullptr;
    }
    if (v.monthly_limit.has_value()) {
        j["monthly_limit"] = v.monthly_limit.value();
    } else {
        j["monthly_limit"] = nullptr;
    }
}

void from_json(const nlohmann::json& j, UsageBucket& v) {
    if (j.contains("utilization") && !j["utilization"].is_null()) {
        v.utilization = j["utilization"].get<double>();
    }
    if (j.contains("resets_at") && !j["resets_at"].is_null()) {
        v.resets_at = j["resets_at"].get<std::string>();
    }
}

void to_json(nlohmann::json& j, const UsageBucket& v) {
    if (v.utilization.has_value()) {
        j["utilization"] = v.utilization.value();
    } else {
        j["utilization"] = nullptr;
    }
    if (v.resets_at.has_value()) {
        j["resets_at"] = v.resets_at.value();
    } else {
        j["resets_at"] = nullptr;
    }
}

void from_json(const nlohmann::json& j, UsageResponse& v) {
    if (j.contains("five_hour") && !j["five_hour"].is_null()) {
        v.five_hour = j["five_hour"].get<UsageBucket>();
    }
    if (j.contains("seven_day") && !j["seven_day"].is_null()) {
        v.seven_day = j["seven_day"].get<UsageBucket>();
    }
    if (j.contains("seven_day_opus") && !j["seven_day_opus"].is_null()) {
        v.seven_day_opus = j["seven_day_opus"].get<UsageBucket>();
    }
    if (j.contains("seven_day_sonnet") && !j["seven_day_sonnet"].is_null()) {
        v.seven_day_sonnet = j["seven_day_sonnet"].get<UsageBucket>();
    }
    if (j.contains("extra_usage") && !j["extra_usage"].is_null()) {
        v.extra_usage = j["extra_usage"].get<ExtraUsage>();
    }
}

void to_json(nlohmann::json& j, const UsageResponse& v) {
    if (v.five_hour.has_value()) {
        j["five_hour"] = v.five_hour.value();
    } else {
        j["five_hour"] = nullptr;
    }
    if (v.seven_day.has_value()) {
        j["seven_day"] = v.seven_day.value();
    } else {
        j["seven_day"] = nullptr;
    }
    if (v.seven_day_opus.has_value()) {
        j["seven_day_opus"] = v.seven_day_opus.value();
    } else {
        j["seven_day_opus"] = nullptr;
    }
    if (v.seven_day_sonnet.has_value()) {
        j["seven_day_sonnet"] = v.seven_day_sonnet.value();
    } else {
        j["seven_day_sonnet"] = nullptr;
    }
    if (v.extra_usage.has_value()) {
        j["extra_usage"] = v.extra_usage.value();
    } else {
        j["extra_usage"] = nullptr;
    }
}

void from_json(const nlohmann::json& j, UsageDataPoint& v) {
    v.id = j.value("id", std::string());
    if (j.contains("timestamp") && j["timestamp"].is_string()) {
        auto parsed = parse_reset_date(j["timestamp"].get<std::string>());
        if (parsed.has_value()) {
            v.timestamp = parsed.value();
        }
    }
    v.pct_5h = j.value("pct_5h", 0.0);
    v.pct_7d = j.value("pct_7d", 0.0);
}

void to_json(nlohmann::json& j, const UsageDataPoint& v) {
    j = nlohmann::json{
        {"id", v.id},
        {"timestamp", format_reset_date(v.timestamp)},
        {"pct_5h", v.pct_5h},
        {"pct_7d", v.pct_7d}
    };
}

void from_json(const nlohmann::json& j, UsageHistory& v) {
    if (j.contains("data_points") && j["data_points"].is_array()) {
        v.data_points = j["data_points"].get<std::vector<UsageDataPoint>>();
    }
}

void to_json(nlohmann::json& j, const UsageHistory& v) {
    j = nlohmann::json{{"data_points", v.data_points}};
}

}  // namespace usage_model
