#pragma once

// Usage history persistence, downsampling, and chart interpolation.
// Ported from macOS: UsageHistoryService.swift, UsageChartView.swift (interpolation)

#include "usage_model.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace history {

// ============================================================
// File I/O
// ============================================================

// Path to history.json in the config directory
std::filesystem::path get_history_file_path();

// Load history from disk (prunes stale entries on load)
usage_model::UsageHistory load_history();
usage_model::UsageHistory load_history(const std::filesystem::path& path);

// Save history to disk
bool save_history(const usage_model::UsageHistory& hist);
bool save_history(const usage_model::UsageHistory& hist,
                  const std::filesystem::path& path);

// ============================================================
// Recording
// ============================================================

// Append a new data point with the current timestamp
void record_data_point(
    usage_model::UsageHistory& hist,
    double pct_5h,
    double pct_7d);

// ============================================================
// Pruning
// ============================================================

// Remove data points older than 30 days
void prune_history(
    std::vector<usage_model::UsageDataPoint>& points,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

// ============================================================
// Downsampling
// ============================================================

// Reduce points to fit a target count by bucket-averaging
std::vector<usage_model::UsageDataPoint> downsample_points(
    const std::vector<usage_model::UsageDataPoint>& points,
    usage_model::TimeRange range,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

// ============================================================
// Catmull-Rom interpolation (for chart hover)
// ============================================================

// Single-dimension Catmull-Rom spline interpolation
double catmull_rom(double p0, double p1, double p2, double p3, double t);

// Interpolate both 5h and 7d values at an arbitrary date within sorted points
std::optional<usage_model::InterpolatedValues> interpolate_values(
    std::chrono::system_clock::time_point date,
    const std::vector<usage_model::UsageDataPoint>& sorted_points);

// Clamp a value to [0.0, 1.0]
double clamp_to_unit_interval(double value);

}  // namespace history
