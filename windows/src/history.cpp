// Implementation — Step 1 Agent C
#include "history.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "config.h"
#include "oauth.h"
#include "string_utils.h"

namespace history {

// ============================================================
// File I/O
// ============================================================

std::filesystem::path get_history_file_path() {
    return oauth::get_config_dir() / "history.json";
}

static usage_model::UsageHistory load_history_from_path(
    const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return usage_model::UsageHistory{};
    }

    try {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            return usage_model::UsageHistory{};
        }

        std::string content((std::istreambuf_iterator<char>(ifs)),
                            std::istreambuf_iterator<char>());
        ifs.close();

        auto j = nlohmann::json::parse(content);
        auto hist = j.get<usage_model::UsageHistory>();

        auto now = std::chrono::system_clock::now();
        prune_history(hist.data_points, now);

        return hist;
    } catch (...) {
        // Corrupt file: rename to .bak.json and return empty
        auto backup = path;
        backup.replace_extension(".bak.json");

        std::error_code ec;
        std::filesystem::remove(backup, ec);
        std::filesystem::rename(path, backup, ec);

        return usage_model::UsageHistory{};
    }
}

usage_model::UsageHistory load_history() {
    return load_history_from_path(get_history_file_path());
}

usage_model::UsageHistory load_history(const std::filesystem::path& path) {
    return load_history_from_path(path);
}

static bool save_history_to_path(
    const usage_model::UsageHistory& hist,
    const std::filesystem::path& path) {
    try {
        auto dir = path.parent_path();
        if (!dir.empty()) {
            std::filesystem::create_directories(dir);
        }

        // Atomic write: write to temp file then rename
        auto tmp_path = path;
        tmp_path += L".tmp";

        nlohmann::json j = hist;
        std::string content = j.dump(2);

        std::ofstream ofs(tmp_path, std::ios::trunc);
        if (!ofs.is_open()) return false;
        ofs << content;
        ofs.close();

        if (!ofs.good()) {
            std::error_code ec;
            std::filesystem::remove(tmp_path, ec);
            return false;
        }

        std::error_code ec;
        std::filesystem::rename(tmp_path, path, ec);
        if (ec) {
            std::filesystem::remove(tmp_path, ec);
            return false;
        }

        return true;
    } catch (...) {
        return false;
    }
}

bool save_history(const usage_model::UsageHistory& hist) {
    return save_history_to_path(hist, get_history_file_path());
}

bool save_history(const usage_model::UsageHistory& hist,
                  const std::filesystem::path& path) {
    return save_history_to_path(hist, path);
}

// ============================================================
// Recording
// ============================================================

void record_data_point(
    usage_model::UsageHistory& hist,
    double pct_5h,
    double pct_7d) {
    usage_model::UsageDataPoint point;
    point.id = string_utils::generate_uuid();
    point.timestamp = std::chrono::system_clock::now();
    point.pct_5h = pct_5h;
    point.pct_7d = pct_7d;

    hist.data_points.push_back(point);
}

// ============================================================
// Pruning
// ============================================================

void prune_history(
    std::vector<usage_model::UsageDataPoint>& points,
    std::chrono::system_clock::time_point now) {
    auto cutoff = now - std::chrono::seconds(config::kHistoryRetentionSeconds);
    points.erase(
        std::remove_if(points.begin(), points.end(),
            [&cutoff](const usage_model::UsageDataPoint& p) {
                return p.timestamp < cutoff;
            }),
        points.end());
}

// ============================================================
// Downsampling
// ============================================================

std::vector<usage_model::UsageDataPoint> downsample_points(
    const std::vector<usage_model::UsageDataPoint>& points,
    usage_model::TimeRange range,
    std::chrono::system_clock::time_point now) {
    auto target_count = usage_model::time_range_target_point_count(range);

    if (static_cast<int>(points.size()) <= target_count) {
        return points;
    }

    auto interval = usage_model::time_range_interval(range);
    auto range_start = now - interval;
    auto bucket_duration_sec =
        static_cast<double>(interval.count()) / static_cast<double>(target_count);

    // Distribute points into buckets
    std::vector<std::vector<const usage_model::UsageDataPoint*>> buckets(
        static_cast<size_t>(target_count));

    for (const auto& point : points) {
        auto offset = std::chrono::duration_cast<std::chrono::duration<double>>(
            point.timestamp - range_start);
        auto idx = static_cast<int>(offset.count() / bucket_duration_sec);
        if (idx < 0) idx = 0;
        if (idx >= target_count) idx = target_count - 1;
        buckets[static_cast<size_t>(idx)].push_back(&point);
    }

    // Average each non-empty bucket
    std::vector<usage_model::UsageDataPoint> result;
    result.reserve(static_cast<size_t>(target_count));

    for (const auto& bucket : buckets) {
        if (bucket.empty()) continue;

        double sum_timestamp = 0.0;
        double sum_5h = 0.0;
        double sum_7d = 0.0;
        auto count = static_cast<double>(bucket.size());

        for (const auto* p : bucket) {
            auto epoch = std::chrono::duration_cast<std::chrono::duration<double>>(
                p->timestamp.time_since_epoch());
            sum_timestamp += epoch.count();
            sum_5h += p->pct_5h;
            sum_7d += p->pct_7d;
        }

        usage_model::UsageDataPoint avg;
        avg.id = bucket.front()->id;
        avg.timestamp = std::chrono::system_clock::time_point(
            std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::duration<double>(sum_timestamp / count)));
        avg.pct_5h = sum_5h / count;
        avg.pct_7d = sum_7d / count;

        result.push_back(avg);
    }

    return result;
}

// ============================================================
// Catmull-Rom interpolation
// ============================================================

double catmull_rom(double p0, double p1, double p2, double p3, double t) {
    double t2 = t * t;
    double t3 = t2 * t;
    return 0.5 * (
        (2.0 * p1) +
        (-p0 + p2) * t +
        (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
        (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3
    );
}

double clamp_to_unit_interval(double value) {
    return (std::max)(0.0, (std::min)(value, 1.0));
}

std::optional<usage_model::InterpolatedValues> interpolate_values(
    std::chrono::system_clock::time_point date,
    const std::vector<usage_model::UsageDataPoint>& sorted_points) {
    if (sorted_points.size() < 2) return std::nullopt;

    // Points must be sorted by timestamp
    auto sorted = sorted_points;
    std::sort(sorted.begin(), sorted.end(),
        [](const usage_model::UsageDataPoint& a,
           const usage_model::UsageDataPoint& b) {
            return a.timestamp < b.timestamp;
        });

    // Out of range: return zeros
    if (date < sorted.front().timestamp || date > sorted.back().timestamp) {
        return usage_model::InterpolatedValues{date, 0.0, 0.0};
    }

    auto size = static_cast<int>(sorted.size());

    for (int i = 0; i < size - 1; ++i) {
        if (date >= sorted[static_cast<size_t>(i)].timestamp &&
            date <= sorted[static_cast<size_t>(i + 1)].timestamp) {
            auto span = std::chrono::duration_cast<std::chrono::duration<double>>(
                sorted[static_cast<size_t>(i + 1)].timestamp -
                sorted[static_cast<size_t>(i)].timestamp);
            double t = 0.0;
            if (span.count() > 0.0) {
                auto offset = std::chrono::duration_cast<std::chrono::duration<double>>(
                    date - sorted[static_cast<size_t>(i)].timestamp);
                t = offset.count() / span.count();
            }

            int i0 = (std::max)(0, i - 1);
            int i3 = (std::min)(size - 1, i + 2);

            double pct_5h = catmull_rom(
                sorted[static_cast<size_t>(i0)].pct_5h,
                sorted[static_cast<size_t>(i)].pct_5h,
                sorted[static_cast<size_t>(i + 1)].pct_5h,
                sorted[static_cast<size_t>(i3)].pct_5h,
                t);
            double pct_7d = catmull_rom(
                sorted[static_cast<size_t>(i0)].pct_7d,
                sorted[static_cast<size_t>(i)].pct_7d,
                sorted[static_cast<size_t>(i + 1)].pct_7d,
                sorted[static_cast<size_t>(i3)].pct_7d,
                t);

            return usage_model::InterpolatedValues{
                date,
                clamp_to_unit_interval(pct_5h),
                clamp_to_unit_interval(pct_7d)
            };
        }
    }

    return std::nullopt;
}

}  // namespace history
