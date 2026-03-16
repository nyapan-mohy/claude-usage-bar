#pragma once

// Windows notification service (balloon / toast).
// Ported from macOS: NotificationService.swift

#include "usage_model.h"

#include <optional>
#include <string>
#include <windows.h>

namespace notification {

// ============================================================
// State (held by main message loop)
// ============================================================

struct NotificationState {
    int threshold_5h = 0;       // 0 = off, 5-100 = alert percentage
    int threshold_7d = 0;
    int threshold_extra = 0;

    // Previous poll values (to detect threshold crossings)
    std::optional<double> previous_pct_5h;
    std::optional<double> previous_pct_7d;
    std::optional<double> previous_pct_extra;
};

// ============================================================
// Core logic
// ============================================================

// Check current values against thresholds and send notifications.
// Updates previous_pct_* in state.
void check_and_notify(
    NotificationState& state,
    double pct_5h,      // 0.0-1.0 ratio
    double pct_7d,
    double pct_extra,
    HWND tray_hwnd);    // HWND for Shell_NotifyIcon balloon

// ============================================================
// Notification delivery
// ============================================================

// Send a Windows balloon notification via Shell_NotifyIcon.
void send_notification(
    HWND tray_hwnd,
    const std::wstring& title,
    const std::wstring& body);

// ============================================================
// Threshold setters (clamp to 0-100)
// ============================================================

void set_threshold_5h(NotificationState& state, int value);
void set_threshold_7d(NotificationState& state, int value);
void set_threshold_extra(NotificationState& state, int value);

// Clamp a threshold value to [0, 100]
int clamp_threshold(int value);

}  // namespace notification
