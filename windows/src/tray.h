#pragma once

// System tray icon management and rendering.
// Ported from macOS: MenuBarIconRenderer.swift, ClaudeUsageBarApp.swift

#include "win32_raii.h"

#include <windows.h>

namespace tray {

// Custom message ID for tray icon callbacks
constexpr UINT WM_TRAYICON = WM_USER + 1;

// Timer IDs
constexpr UINT_PTR TIMER_POLLING     = 1;
constexpr UINT_PTR TIMER_HISTORY_FLUSH = 2;

// ============================================================
// Tray icon lifecycle
// ============================================================

// Add the tray icon to the system notification area
bool create_tray_icon(HWND hwnd, HICON icon);

// Update the tray icon image (e.g. after usage changes)
void update_tray_icon(HWND hwnd, HICON icon);

// Update the tray icon tooltip text
void update_tray_tooltip(HWND hwnd, const wchar_t* tooltip);

// Remove the tray icon on shutdown
void remove_tray_icon(HWND hwnd);

// ============================================================
// Icon rendering (GDI+)
// ============================================================

// Render a 16x16 tray icon showing two progress bars (5h and 7d).
// pct_5h, pct_7d: 0.0-1.0
win32_raii::UniqueIcon render_tray_icon(double pct_5h, double pct_7d);

// Render a 16x16 tray icon with dashed (empty) bars for unauthenticated state.
win32_raii::UniqueIcon render_unauthenticated_icon();

}  // namespace tray
