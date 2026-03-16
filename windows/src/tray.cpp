// System tray icon management and GDI+ icon rendering.
// Ported from macOS: MenuBarIconRenderer.swift

#include "tray.h"
#include "config.h"

#include <shellapi.h>
#include <algorithm>

#pragma comment(lib, "gdiplus.lib")

namespace tray {

// ============================================================
// Internal constants (derived from macOS MenuBarIconRenderer.swift,
// adapted for 16x16 pixel system tray icons)
// ============================================================

namespace {

constexpr int kIconSize = config::kTrayIconSize;  // 16
constexpr int kBarWidth = 10;
constexpr int kBarHeight = 3;
constexpr int kRowGap = 2;
constexpr int kBarX = 3;  // left margin for centering bars

// Vertical centering: total height = 3 + 2 + 3 = 8, margin = (16 - 8) / 2 = 4
constexpr int kTopBarY = 4;
constexpr int kBottomBarY = kTopBarY + kBarHeight + kRowGap;  // 9

constexpr UINT kTrayUid = 1;

// Fill a NOTIFYICONDATAW with common fields
NOTIFYICONDATAW make_nid(HWND hwnd) {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayUid;
    return nid;
}

// Draw a single progress bar (background + fill)
void draw_progress_bar(Gdiplus::Graphics& g, int x, int y,
                       int width, int height, double pct) {
    // Background (semi-transparent white)
    Gdiplus::SolidBrush bg_brush(Gdiplus::Color(64, 255, 255, 255));
    g.FillRectangle(&bg_brush, x, y, width, height);

    // Foreground fill
    double clamped = (std::max)(0.0, (std::min)(1.0, pct));
    if (clamped > 0.0) {
        int fill_width = static_cast<int>(width * clamped);
        if (fill_width < 1 && clamped > 0.0) fill_width = 1;
        Gdiplus::SolidBrush fg_brush(Gdiplus::Color(255, 255, 255, 255));
        g.FillRectangle(&fg_brush, x, y, fill_width, height);
    }
}

// Draw a dashed bar outline (for unauthenticated state)
void draw_dashed_bar(Gdiplus::Graphics& g, int x, int y,
                     int width, int height) {
    Gdiplus::Pen pen(Gdiplus::Color(64, 255, 255, 255), 1.0f);
    pen.SetDashStyle(Gdiplus::DashStyleDash);
    g.DrawRectangle(&pen, x, y, width - 1, height - 1);
}

// Convert a GDI+ Bitmap to an HICON via ICONINFO + CreateIconIndirect
win32_raii::UniqueIcon bitmap_to_icon(Gdiplus::Bitmap& bitmap) {
    HBITMAP hbm_color = nullptr;
    bitmap.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hbm_color);
    if (!hbm_color) return win32_raii::UniqueIcon(nullptr);

    // Create an empty monochrome mask bitmap
    auto hbm_mask = win32_raii::UniqueBitmap(
        ::CreateBitmap(kIconSize, kIconSize, 1, 1, nullptr));

    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmMask = hbm_mask.get();
    ii.hbmColor = hbm_color;

    HICON hicon = ::CreateIconIndirect(&ii);

    // Clean up the color bitmap (CreateIconIndirect makes a copy)
    ::DeleteObject(hbm_color);

    return win32_raii::make_icon(hicon);
}

}  // anonymous namespace

// ============================================================
// Tray icon lifecycle
// ============================================================

bool create_tray_icon(HWND hwnd, HICON icon) {
    NOTIFYICONDATAW nid = make_nid(hwnd);
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = icon;
    wcscpy_s(nid.szTip, config::kAppName);
    return Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
}

void update_tray_icon(HWND hwnd, HICON icon) {
    NOTIFYICONDATAW nid = make_nid(hwnd);
    nid.uFlags = NIF_ICON;
    nid.hIcon = icon;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void update_tray_tooltip(HWND hwnd, const wchar_t* tooltip) {
    NOTIFYICONDATAW nid = make_nid(hwnd);
    nid.uFlags = NIF_TIP;
    if (tooltip) {
        wcsncpy_s(nid.szTip, tooltip, _TRUNCATE);
    }
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void remove_tray_icon(HWND hwnd) {
    NOTIFYICONDATAW nid = make_nid(hwnd);
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

// ============================================================
// Icon rendering (GDI+)
// ============================================================

win32_raii::UniqueIcon render_tray_icon(double pct_5h, double pct_7d) {
    Gdiplus::Bitmap bitmap(kIconSize, kIconSize, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&bitmap);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

    draw_progress_bar(g, kBarX, kTopBarY, kBarWidth, kBarHeight, pct_5h);
    draw_progress_bar(g, kBarX, kBottomBarY, kBarWidth, kBarHeight, pct_7d);

    return bitmap_to_icon(bitmap);
}

win32_raii::UniqueIcon render_unauthenticated_icon() {
    Gdiplus::Bitmap bitmap(kIconSize, kIconSize, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&bitmap);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

    draw_dashed_bar(g, kBarX, kTopBarY, kBarWidth, kBarHeight);
    draw_dashed_bar(g, kBarX, kBottomBarY, kBarWidth, kBarHeight);

    return bitmap_to_icon(bitmap);
}

}  // namespace tray
