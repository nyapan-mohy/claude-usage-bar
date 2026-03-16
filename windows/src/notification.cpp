// Implementation — Step 1 Agent C
#include "notification.h"

#include <algorithm>
#include <shellapi.h>

#include "config.h"
#include "usage_model.h"

namespace notification {

// ============================================================
// Core logic
// ============================================================

void check_and_notify(
    NotificationState& state,
    double pct_5h,
    double pct_7d,
    double pct_extra,
    HWND tray_hwnd) {
    double current_5h = pct_5h * 100.0;
    double current_7d = pct_7d * 100.0;
    double current_extra = pct_extra * 100.0;

    double prev_5h = state.previous_pct_5h.value_or(0.0);
    double prev_7d = state.previous_pct_7d.value_or(0.0);
    double prev_extra = state.previous_pct_extra.value_or(0.0);

    auto alerts = usage_model::crossed_thresholds(
        state.threshold_5h,
        state.threshold_7d,
        state.threshold_extra,
        prev_5h, prev_7d, prev_extra,
        current_5h, current_7d, current_extra);

    for (const auto& alert : alerts) {
        std::wstring title = L"Claude Usage";
        std::wstring body = alert.window + L" usage has reached " +
                           std::to_wstring(alert.pct) + L"%";
        send_notification(tray_hwnd, title, body);
    }

    state.previous_pct_5h = current_5h;
    state.previous_pct_7d = current_7d;
    state.previous_pct_extra = current_extra;
}

// ============================================================
// Notification delivery
// ============================================================

void send_notification(
    HWND tray_hwnd,
    const std::wstring& title,
    const std::wstring& body) {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = tray_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;

    wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, body.c_str(), _TRUNCATE);

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

// ============================================================
// Threshold setters
// ============================================================

int clamp_threshold(int value) {
    return (std::max)(config::kThresholdMin, (std::min)(value, config::kThresholdMax));
}

void set_threshold_5h(NotificationState& state, int value) {
    state.threshold_5h = clamp_threshold(value);
    state.previous_pct_5h = std::nullopt;
}

void set_threshold_7d(NotificationState& state, int value) {
    state.threshold_7d = clamp_threshold(value);
    state.previous_pct_7d = std::nullopt;
}

void set_threshold_extra(NotificationState& state, int value) {
    state.threshold_extra = clamp_threshold(value);
    state.previous_pct_extra = std::nullopt;
}

}  // namespace notification
