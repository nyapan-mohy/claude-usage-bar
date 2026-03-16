#pragma once

// Popup window displayed when the user clicks the system tray icon.
// Ported from macOS: PopoverView.swift, UsageChartView.swift, SettingsView.swift

#include "usage_service.h"
#include "history.h"
#include "notification.h"

#include <windows.h>

namespace popup {

// ============================================================
// Popup window
// ============================================================

// Create the popup window (initially hidden).
// Returns the HWND of the popup, or nullptr on failure.
HWND create_popup_window(HINSTANCE instance);

// Show the popup near the tray icon position
void show_popup(HWND popup, HWND tray_hwnd);

// Hide the popup
void hide_popup(HWND popup);

// Toggle popup visibility
void toggle_popup(HWND popup, HWND tray_hwnd);

// ============================================================
// Content update
// ============================================================

// Repaint the popup with the latest service state and history
void update_popup_content(
    HWND popup,
    const usage_service::UsageServiceState& state,
    const usage_model::UsageHistory& history);

// ============================================================
// Settings dialog
// ============================================================

// Show a modal settings dialog
void show_settings_dialog(
    HWND parent,
    usage_service::UsageServiceState& state,
    notification::NotificationState& notif_state);

}  // namespace popup
