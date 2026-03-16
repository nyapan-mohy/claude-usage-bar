// Application entry point — system tray + message loop.
// Ported from macOS: ClaudeUsageBarApp.swift

#include "config.h"
#include "history.h"
#include "http_client.h"
#include "notification.h"
#include "popup.h"
#include "settings.h"
#include "tray.h"
#include "usage_service.h"
#include "win32_raii.h"

#include <shellapi.h>
#include <windows.h>

// ============================================================
// Menu command IDs
// ============================================================

namespace {

constexpr UINT ID_REFRESH  = 40001;
constexpr UINT ID_SETTINGS = 40002;
constexpr UINT ID_QUIT     = 40003;

constexpr wchar_t kWindowClassName[] = L"ClaudeUsageBarHiddenWindow";

// ============================================================
// Application state (owned by the message loop)
// ============================================================

struct AppState {
    usage_service::UsageServiceState service;
    usage_model::UsageHistory history;
    notification::NotificationState notif;
    win32_raii::UniqueIcon current_icon;
    HWND popup_hwnd = nullptr;
    HINSTANCE hinstance = nullptr;
};

// Global pointer — only valid during the lifetime of WinMain.
// Required because WndProc has a fixed signature and cannot carry user data
// until WM_NCCREATE (hidden windows are created before messages flow).
AppState* g_state = nullptr;

// ============================================================
// Timer helpers
// ============================================================

void start_polling_timer(HWND hwnd, int polling_minutes) {
    UINT interval_ms = static_cast<UINT>(polling_minutes) * 60u * 1000u;
    if (interval_ms == 0) interval_ms = config::kDefaultPollingMinutes * 60u * 1000u;
    ::SetTimer(hwnd, tray::TIMER_POLLING, interval_ms, nullptr);
}

void start_history_flush_timer(HWND hwnd) {
    UINT interval_ms = static_cast<UINT>(config::kHistoryFlushInterval) * 1000u;
    ::SetTimer(hwnd, tray::TIMER_HISTORY_FLUSH, interval_ms, nullptr);
}

// ============================================================
// Refresh usage data + update tray icon
// ============================================================

void do_refresh(HWND hwnd) {
    if (!g_state) return;

    usage_service::fetch_usage(g_state->service, http_client::winhttp_request);

    // Update icon
    auto new_icon = g_state->service.is_authenticated
        ? tray::render_tray_icon(g_state->service.pct_5h(), g_state->service.pct_7d())
        : tray::render_unauthenticated_icon();
    tray::update_tray_icon(hwnd, new_icon.get());
    g_state->current_icon = std::move(new_icon);

    // Record history data point
    if (g_state->service.is_authenticated) {
        history::record_data_point(
            g_state->history,
            g_state->service.pct_5h(),
            g_state->service.pct_7d());
    }

    // Check notification thresholds
    notification::check_and_notify(
        g_state->notif,
        g_state->service.pct_5h(),
        g_state->service.pct_7d(),
        g_state->service.pct_extra(),
        hwnd);

    // Update popup content
    if (g_state->popup_hwnd) {
        popup::update_popup_content(
            g_state->popup_hwnd,
            g_state->service,
            g_state->history);
    }

    // Re-set timer if polling interval changed
    start_polling_timer(hwnd, g_state->service.polling_minutes);
}

// ============================================================
// Context menu (right-click on tray icon)
// ============================================================

void show_context_menu(HWND hwnd) {
    POINT pt = {};
    ::GetCursorPos(&pt);

    HMENU menu = ::CreatePopupMenu();
    if (!menu) return;

    ::AppendMenuW(menu, MF_STRING, ID_REFRESH, L"Refresh");
    ::AppendMenuW(menu, MF_STRING, ID_SETTINGS, L"Settings...");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, ID_QUIT, L"Quit");

    // Required for TrackPopupMenu to work from a notification icon
    ::SetForegroundWindow(hwnd);
    ::TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    ::PostMessageW(hwnd, WM_NULL, 0, 0);

    ::DestroyMenu(menu);
}

// ============================================================
// Window procedure
// ============================================================

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case tray::WM_TRAYICON:
        if (LOWORD(lParam) == WM_LBUTTONUP) {
            if (g_state && g_state->popup_hwnd) {
                popup::toggle_popup(g_state->popup_hwnd, hwnd);
            }
        } else if (LOWORD(lParam) == WM_RBUTTONUP) {
            show_context_menu(hwnd);
        }
        return 0;

    case WM_TIMER:
        if (wParam == tray::TIMER_POLLING) {
            do_refresh(hwnd);
        } else if (wParam == tray::TIMER_HISTORY_FLUSH) {
            if (g_state) {
                history::save_history(g_state->history);
            }
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_REFRESH:
            do_refresh(hwnd);
            break;
        case ID_SETTINGS:
            if (g_state && g_state->popup_hwnd) {
                popup::show_settings_dialog(
                    g_state->popup_hwnd,
                    g_state->service,
                    g_state->notif);
            }
            break;
        case ID_QUIT:
            ::DestroyWindow(hwnd);
            break;
        }
        return 0;

    case WM_DESTROY:
        if (g_state) {
            history::save_history(g_state->history);
            tray::remove_tray_icon(hwnd);
        }
        ::PostQuitMessage(0);
        return 0;

    default:
        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // anonymous namespace

// ============================================================
// WinMain
// ============================================================

int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_ LPSTR /*lpCmdLine*/,
    _In_ int /*nCmdShow*/)
{
    // GDI+ initialization (scoped — shutdown on return)
    win32_raii::GdiplusScope gdiplus;

    // DPI awareness
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Initialize application state
    AppState state = {};
    state.hinstance = hInstance;
    state.service = usage_service::init();
    state.history = history::load_history();

    // Load notification thresholds from settings
    auto app_settings = settings::load_settings();
    state.notif.threshold_5h = app_settings.threshold_5h;
    state.notif.threshold_7d = app_settings.threshold_7d;
    state.notif.threshold_extra = app_settings.threshold_extra;

    g_state = &state;

    // Register hidden window class (message-only receiver)
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kWindowClassName;

    if (!::RegisterClassExW(&wc)) {
        g_state = nullptr;
        return 1;
    }

    // Create the hidden message window
    HWND hwnd = ::CreateWindowExW(
        0, kWindowClassName, L"",
        0,  // no visible style
        0, 0, 0, 0,
        HWND_MESSAGE,  // message-only window
        nullptr, hInstance, nullptr);

    if (!hwnd) {
        g_state = nullptr;
        return 1;
    }

    // Render initial tray icon
    state.current_icon = state.service.is_authenticated
        ? tray::render_tray_icon(state.service.pct_5h(), state.service.pct_7d())
        : tray::render_unauthenticated_icon();

    tray::create_tray_icon(hwnd, state.current_icon.get());

    // Create popup window (may return nullptr if Agent F has not yet implemented)
    state.popup_hwnd = popup::create_popup_window(hInstance);

    // Start polling timer if authenticated
    if (state.service.is_authenticated) {
        start_polling_timer(hwnd, state.service.polling_minutes);

        // Trigger initial fetch via a posted message so the message loop is running
        ::PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_REFRESH, 0), 0);
    }

    // History flush timer
    start_history_flush_timer(hwnd);

    // Message loop
    MSG msg = {};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    g_state = nullptr;
    return static_cast<int>(msg.wParam);
}
