// Popup window — usage display, chart, and settings dialog.
// Ported from macOS: PopoverView.swift, UsageChartView.swift, SettingsView.swift

#include "popup.h"

#include "config.h"
#include "history.h"
#include "notification.h"
#include "settings.h"
#include "string_utils.h"
#include "usage_model.h"
#include "usage_service.h"
#include "win32_raii.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

// Ensure GDI+ headers have min/max
using std::min;
using std::max;
#include <objidl.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <shellapi.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")

// ============================================================
// WM_COMMAND IDs used by the main window (from tray.h / main.cpp)
// ============================================================
#ifndef ID_REFRESH
#define ID_REFRESH  40001
#endif
#ifndef ID_SIGN_IN
#define ID_SIGN_IN  40002
#endif
#ifndef ID_SIGN_OUT
#define ID_SIGN_OUT 40003
#endif
#ifndef ID_SUBMIT_CODE
#define ID_SUBMIT_CODE 40004
#endif

namespace popup {

// ============================================================
// Layout constants
// ============================================================
namespace {

constexpr int kPopupWidth   = 360;
constexpr int kPopupHeight  = 620;
constexpr int kMargin       = 16;
constexpr int kBarHeight    = 16;
constexpr int kChartHeight  = 130;
constexpr int kSectionGap   = 12;
constexpr int kRowHeight    = 48;
constexpr int kBtnHeight    = 28;
constexpr int kBtnWidth     = 80;
constexpr int kRangeButtonW = 42;
constexpr int kRangeButtonH = 26;
constexpr int kHeaderHeight = 36;
constexpr int kFooterHeight = 70;

constexpr float kFontSizeHeader   = 18.0f;
constexpr float kFontSizeLabel    = 13.0f;
constexpr float kFontSizeSmall    = 11.0f;
constexpr float kFontSizeButton   = 12.0f;

// Colors
inline Gdiplus::Color bg_color()          { return {255, 30, 30, 30}; }
inline Gdiplus::Color text_white()        { return {255, 240, 240, 240}; }
inline Gdiplus::Color text_secondary()    { return {180, 160, 160, 160}; }
inline Gdiplus::Color text_error()        { return {255, 239, 68, 68}; }
inline Gdiplus::Color bar_bg()            { return {80, 255, 255, 255}; }
inline Gdiplus::Color bar_blue()          { return {255, 96, 165, 250}; }
inline Gdiplus::Color bar_green()         { return {255, 34, 197, 94}; }
inline Gdiplus::Color bar_yellow()        { return {255, 234, 179, 8}; }
inline Gdiplus::Color bar_red()           { return {255, 239, 68, 68}; }
inline Gdiplus::Color chart_5h()          { return {255, 59, 130, 246}; }
inline Gdiplus::Color chart_7d()          { return {255, 249, 115, 22}; }
inline Gdiplus::Color divider_color()     { return {60, 255, 255, 255}; }
inline Gdiplus::Color btn_bg()            { return {40, 255, 255, 255}; }
inline Gdiplus::Color btn_primary()       { return {255, 59, 130, 246}; }
inline Gdiplus::Color grid_line_color()   { return {30, 255, 255, 255}; }
inline Gdiplus::Color range_sel_bg()      { return {80, 59, 130, 246}; }
inline Gdiplus::Color tooltip_bg()        { return {230, 50, 50, 50}; }

// Pick bar color based on utilization percentage (0-1)
Gdiplus::Color color_for_pct(double pct) {
    if (pct < 0.60) return bar_green();
    if (pct < 0.80) return bar_yellow();
    return bar_red();
}

// ============================================================
// Static popup state
// ============================================================

usage_service::UsageServiceState s_state;
usage_model::UsageHistory s_history;

usage_model::TimeRange s_selected_range = usage_model::TimeRange::Day1;
std::optional<POINT> s_hover_point;

// Hit-test regions (recalculated in WM_PAINT)
RECT s_chart_area = {};
RECT s_sign_in_btn = {};
RECT s_submit_btn = {};
RECT s_refresh_btn = {};
RECT s_settings_btn = {};
std::vector<RECT> s_range_buttons;  // one per TimeRange value

// Native EDIT control for code entry (handles paste, selection, cursor natively)
HWND s_code_edit = nullptr;

// Registered popup class name
const wchar_t* const kPopupClassName = L"ClaudeUsagePopup";

// ============================================================
// Forward declarations
// ============================================================

LRESULT CALLBACK popup_wndproc(HWND, UINT, WPARAM, LPARAM);

// Time formatting helpers
std::wstring format_relative_time(std::chrono::system_clock::time_point then);
std::wstring format_reset_time(std::chrono::system_clock::time_point reset);

// Drawing helpers
void draw_popup(HWND hwnd, HDC hdc);
void draw_header(Gdiplus::Graphics& g, int& y);
void draw_sign_in(Gdiplus::Graphics& g, int& y);
void draw_code_entry(Gdiplus::Graphics& g, int& y);
void draw_usage_bars(Gdiplus::Graphics& g, int& y);
void draw_extra_usage(Gdiplus::Graphics& g, int& y);
void draw_divider(Gdiplus::Graphics& g, int y);
void draw_chart(Gdiplus::Graphics& g, int& y);
void draw_chart_hover(Gdiplus::Graphics& g,
                      const std::vector<usage_model::UsageDataPoint>& points);
void draw_footer(Gdiplus::Graphics& g, int& y);
void draw_error(Gdiplus::Graphics& g, int& y);

void draw_progress_bar(Gdiplus::Graphics& g, int x, int y, int w, int h,
                       double pct, Gdiplus::Color fill);
void draw_rounded_rect(Gdiplus::Graphics& g, Gdiplus::Brush& brush,
                       int x, int y, int w, int h, int radius);
void draw_rounded_rect_outline(Gdiplus::Graphics& g, Gdiplus::Pen& pen,
                               int x, int y, int w, int h, int radius);
void draw_text_left(Gdiplus::Graphics& g, const std::wstring& text,
                    const Gdiplus::Font& font, Gdiplus::Color color,
                    int x, int y, int w);
void draw_text_right(Gdiplus::Graphics& g, const std::wstring& text,
                     const Gdiplus::Font& font, Gdiplus::Color color,
                     int x, int y, int w);
void draw_text_center(Gdiplus::Graphics& g, const std::wstring& text,
                      const Gdiplus::Font& font, Gdiplus::Color color,
                      int x, int y, int w, int h);

// ============================================================
// Time range helpers
// ============================================================

constexpr int kTimeRangeCount = 5;

usage_model::TimeRange time_range_from_index(int idx) {
    constexpr usage_model::TimeRange ranges[] = {
        usage_model::TimeRange::Hour1,
        usage_model::TimeRange::Hour6,
        usage_model::TimeRange::Day1,
        usage_model::TimeRange::Day7,
        usage_model::TimeRange::Day30,
    };
    if (idx < 0 || idx >= kTimeRangeCount) return usage_model::TimeRange::Day1;
    return ranges[idx];
}

int time_range_to_index(usage_model::TimeRange r) {
    switch (r) {
        case usage_model::TimeRange::Hour1:  return 0;
        case usage_model::TimeRange::Hour6:  return 1;
        case usage_model::TimeRange::Day1:   return 2;
        case usage_model::TimeRange::Day7:   return 3;
        case usage_model::TimeRange::Day30:  return 4;
    }
    return 2;
}

const wchar_t* time_range_label_w(usage_model::TimeRange r) {
    switch (r) {
        case usage_model::TimeRange::Hour1:  return L"1h";
        case usage_model::TimeRange::Hour6:  return L"6h";
        case usage_model::TimeRange::Day1:   return L"1d";
        case usage_model::TimeRange::Day7:   return L"7d";
        case usage_model::TimeRange::Day30:  return L"30d";
    }
    return L"1d";
}

}  // anonymous namespace

// ============================================================
// Public API
// ============================================================

HWND create_popup_window(HINSTANCE instance) {
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = popup_wndproc;
    wc.hInstance      = instance;
    wc.lpszClassName  = kPopupClassName;
    wc.hCursor        = LoadCursorW(nullptr, IDC_ARROW);
    wc.style          = CS_DROPSHADOW;

    if (!RegisterClassExW(&wc)) return nullptr;

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kPopupClassName,
        L"Claude Usage",
        WS_POPUP | WS_BORDER,
        -9999, -9999,   // off-screen initially
        kPopupWidth, kPopupHeight,
        nullptr, nullptr, instance, nullptr
    );

    if (hwnd) {
        // Create native EDIT control for OAuth code entry (handles Ctrl+V natively)
        s_code_edit = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
            kMargin, 0,  // y will be repositioned in draw_code_entry
            kPopupWidth - kMargin * 2, 28,
            hwnd, nullptr, instance, nullptr
        );
        if (s_code_edit) {
            // Set font
            HFONT edit_font = CreateFontW(
                -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
            SendMessageW(s_code_edit, WM_SETFONT,
                         reinterpret_cast<WPARAM>(edit_font), TRUE);
            // Set placeholder text
            SendMessageW(s_code_edit, EM_SETCUEBANNER, TRUE,
                         reinterpret_cast<LPARAM>(L"code#state"));
        }
    }

    return hwnd;
}

void show_popup(HWND popup, HWND /*tray_hwnd*/) {
    if (!popup) return;

    POINT cursor;
    GetCursorPos(&cursor);

    RECT work_area = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);

    int x = cursor.x - kPopupWidth / 2;
    int y = work_area.bottom - kPopupHeight;

    // Clamp to work area
    if (x < work_area.left) x = work_area.left;
    if (x + kPopupWidth > work_area.right) x = work_area.right - kPopupWidth;
    if (y < work_area.top) y = work_area.top;

    SetWindowPos(popup, HWND_TOPMOST, x, y, kPopupWidth, kPopupHeight,
                 SWP_NOACTIVATE);
    ShowWindow(popup, SW_SHOWNA);
    SetForegroundWindow(popup);
}

void hide_popup(HWND popup) {
    if (popup) ShowWindow(popup, SW_HIDE);
}

void toggle_popup(HWND popup, HWND tray_hwnd) {
    if (!popup) return;
    if (IsWindowVisible(popup)) {
        hide_popup(popup);
    } else {
        show_popup(popup, tray_hwnd);
    }
}

void update_popup_content(
    HWND popup,
    const usage_service::UsageServiceState& state,
    const usage_model::UsageHistory& history)
{
    s_state   = state;
    s_history = history;
    if (popup && IsWindowVisible(popup)) {
        InvalidateRect(popup, nullptr, FALSE);
    }
}

// ============================================================
// Settings dialog
// ============================================================

namespace {

// Dialog control IDs
constexpr int IDC_POLLING_COMBO   = 100;
constexpr int IDC_THRESHOLD_5H   = 101;
constexpr int IDC_THRESHOLD_7D   = 102;
constexpr int IDC_THRESHOLD_EX   = 103;
constexpr int IDC_LABEL_5H       = 104;
constexpr int IDC_LABEL_7D       = 105;
constexpr int IDC_LABEL_EX       = 106;
constexpr int IDC_STARTUP_CHECK  = 107;
constexpr int IDC_SIGN_OUT_BTN   = 108;
constexpr int IDC_OK_BTN         = IDOK;
constexpr int IDC_CANCEL_BTN     = IDCANCEL;

struct SettingsDialogData {
    usage_service::UsageServiceState* state;
    notification::NotificationState* notif;
    int polling_minutes;
    int threshold_5h;
    int threshold_7d;
    int threshold_extra;
    bool launch_at_login;
};

void update_threshold_label(HWND dlg, int label_id, int value) {
    wchar_t buf[32];
    if (value > 0) {
        swprintf(buf, 32, L"%d%%", value);
    } else {
        swprintf(buf, 32, L"Off");
    }
    SetDlgItemTextW(dlg, label_id, buf);
}

LRESULT CALLBACK settings_dlg_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* data = reinterpret_cast<SettingsDialogData*>(
        GetWindowLongPtrW(dlg, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        data = reinterpret_cast<SettingsDialogData*>(cs->lpCreateParams);
        SetWindowLongPtrW(dlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));

        HFONT font = CreateFontW(
            -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        int y = 16;
        int lx = 16;
        int rw = 300;

        // Section: General
        CreateWindowExW(0, L"STATIC", L"General",
            WS_CHILD | WS_VISIBLE, lx, y, rw, 20, dlg, nullptr, nullptr, nullptr);
        y += 28;

        // Polling interval label
        CreateWindowExW(0, L"STATIC", L"Polling Interval:",
            WS_CHILD | WS_VISIBLE, lx, y + 2, 120, 20, dlg, nullptr, nullptr, nullptr);

        // Polling combo box
        HWND combo = CreateWindowExW(0, L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            lx + 130, y, 160, 200, dlg,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_POLLING_COMBO)),
            nullptr, nullptr);

        int sel_index = 0;
        for (size_t i = 0; i < config::kPollingOptions.size(); ++i) {
            auto label = usage_model::polling_option_label(config::kPollingOptions[i]);
            SendMessageW(combo, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(label.c_str()));
            if (config::kPollingOptions[i] == data->polling_minutes) {
                sel_index = static_cast<int>(i);
            }
        }
        SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(sel_index), 0);
        y += 34;

        // Launch at login
        CreateWindowExW(0, L"BUTTON", L"Launch at Login",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            lx, y, rw, 20, dlg,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STARTUP_CHECK)),
            nullptr, nullptr);
        if (data->launch_at_login) {
            CheckDlgButton(dlg, IDC_STARTUP_CHECK, BST_CHECKED);
        }
        y += 34;

        // Section: Notifications
        CreateWindowExW(0, L"STATIC", L"Notifications",
            WS_CHILD | WS_VISIBLE, lx, y, rw, 20, dlg, nullptr, nullptr, nullptr);
        y += 28;

        // Threshold sliders
        auto create_slider = [&](const wchar_t* label, int slider_id,
                                  int label_id, int value) {
            CreateWindowExW(0, L"STATIC", label,
                WS_CHILD | WS_VISIBLE, lx, y + 2, 100, 20, dlg,
                nullptr, nullptr, nullptr);
            CreateWindowExW(0, L"STATIC", L"Off",
                WS_CHILD | WS_VISIBLE | SS_RIGHT, lx + rw - 50, y + 2, 50, 20, dlg,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(label_id)),
                nullptr, nullptr);
            y += 22;

            HWND slider = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr,
                WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                lx, y, rw, 26, dlg,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(slider_id)),
                nullptr, nullptr);
            SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(slider, TBM_SETTICFREQ, 5, 0);
            SendMessageW(slider, TBM_SETLINESIZE, 0, 5);
            SendMessageW(slider, TBM_SETPAGESIZE, 0, 5);
            SendMessageW(slider, TBM_SETPOS, TRUE, value);
            update_threshold_label(dlg, label_id, value);
            y += 30;
        };

        create_slider(L"5-hour window", IDC_THRESHOLD_5H, IDC_LABEL_5H,
                       data->threshold_5h);
        create_slider(L"7-day window", IDC_THRESHOLD_7D, IDC_LABEL_7D,
                       data->threshold_7d);
        create_slider(L"Extra usage", IDC_THRESHOLD_EX, IDC_LABEL_EX,
                       data->threshold_extra);

        // Section: Account
        if (data->state && data->state->is_authenticated) {
            CreateWindowExW(0, L"STATIC", L"Account",
                WS_CHILD | WS_VISIBLE, lx, y, rw, 20, dlg, nullptr, nullptr, nullptr);
            y += 24;

            if (data->state->account_email) {
                auto email_w = string_utils::utf8_to_wide(
                    data->state->account_email.value());
                CreateWindowExW(0, L"STATIC", email_w.c_str(),
                    WS_CHILD | WS_VISIBLE, lx, y, rw, 20, dlg,
                    nullptr, nullptr, nullptr);
                y += 24;
            }

            CreateWindowExW(0, L"BUTTON", L"Sign Out",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                lx, y, 80, 26, dlg,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SIGN_OUT_BTN)),
                nullptr, nullptr);
            y += 36;
        }

        // OK / Cancel
        y += 8;
        CreateWindowExW(0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            lx + rw - 170, y, 80, 28, dlg,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_OK_BTN)),
            nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            lx + rw - 80, y, 80, 28, dlg,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CANCEL_BTN)),
            nullptr, nullptr);

        // Apply font to all children
        EnumChildWindows(dlg, [](HWND child, LPARAM lp) -> BOOL {
            SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(lp), TRUE);
            return TRUE;
        }, reinterpret_cast<LPARAM>(font));

        return 0;
    }

    case WM_HSCROLL: {
        if (!data) break;
        auto slider = reinterpret_cast<HWND>(lp);
        int id = GetDlgCtrlID(slider);
        int pos = static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0));
        // Snap to step of 5
        pos = (pos / config::kThresholdStep) * config::kThresholdStep;

        if (id == IDC_THRESHOLD_5H) {
            data->threshold_5h = pos;
            update_threshold_label(dlg, IDC_LABEL_5H, pos);
        } else if (id == IDC_THRESHOLD_7D) {
            data->threshold_7d = pos;
            update_threshold_label(dlg, IDC_LABEL_7D, pos);
        } else if (id == IDC_THRESHOLD_EX) {
            data->threshold_extra = pos;
            update_threshold_label(dlg, IDC_LABEL_EX, pos);
        }
        return 0;
    }

    case WM_COMMAND: {
        if (!data) break;
        int id = LOWORD(wp);

        if (id == IDC_OK_BTN) {
            // Read polling combo
            HWND combo = GetDlgItem(dlg, IDC_POLLING_COMBO);
            int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
            if (sel >= 0 && sel < static_cast<int>(config::kPollingOptions.size())) {
                data->polling_minutes = config::kPollingOptions[static_cast<size_t>(sel)];
            }
            data->launch_at_login =
                IsDlgButtonChecked(dlg, IDC_STARTUP_CHECK) == BST_CHECKED;

            // Apply settings
            if (data->state) {
                usage_service::update_polling_interval(
                    *data->state, data->polling_minutes);
            }
            if (data->notif) {
                notification::set_threshold_5h(*data->notif, data->threshold_5h);
                notification::set_threshold_7d(*data->notif, data->threshold_7d);
                notification::set_threshold_extra(*data->notif, data->threshold_extra);
            }

            // Save to settings file
            settings::AppSettings app_settings;
            app_settings.polling_minutes  = data->polling_minutes;
            app_settings.threshold_5h     = data->threshold_5h;
            app_settings.threshold_7d     = data->threshold_7d;
            app_settings.threshold_extra  = data->threshold_extra;
            app_settings.launch_at_login  = data->launch_at_login;
            app_settings.setup_complete   = true;
            settings::save_settings(app_settings);
            settings::set_launch_at_login(data->launch_at_login);

            DestroyWindow(dlg);
            return 0;
        }

        if (id == IDC_CANCEL_BTN) {
            DestroyWindow(dlg);
            return 0;
        }

        if (id == IDC_SIGN_OUT_BTN) {
            if (data->state) {
                usage_service::sign_out(*data->state);
            }
            DestroyWindow(dlg);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(dlg);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(dlg, msg, wp, lp);
}

const wchar_t* const kSettingsClassName = L"ClaudeUsageSettings";
bool s_settings_class_registered = false;

}  // anonymous namespace

void show_settings_dialog(
    HWND parent,
    usage_service::UsageServiceState& state,
    notification::NotificationState& notif_state)
{
    // Load current settings
    auto app_settings = settings::load_settings();

    static SettingsDialogData dlg_data;
    dlg_data.state           = &state;
    dlg_data.notif           = &notif_state;
    dlg_data.polling_minutes = state.polling_minutes;
    dlg_data.threshold_5h    = notif_state.threshold_5h;
    dlg_data.threshold_7d    = notif_state.threshold_7d;
    dlg_data.threshold_extra = notif_state.threshold_extra;
    dlg_data.launch_at_login = app_settings.launch_at_login;

    HINSTANCE hinst = reinterpret_cast<HINSTANCE>(
        GetWindowLongPtrW(parent, GWLP_HINSTANCE));

    if (!s_settings_class_registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = settings_dlg_proc;
        wc.hInstance      = hinst;
        wc.lpszClassName  = kSettingsClassName;
        wc.hCursor        = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground  = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassExW(&wc);
        s_settings_class_registered = true;
    }

    constexpr int dlgW = 360;
    constexpr int dlgH = 480;

    RECT parent_rect;
    GetWindowRect(parent, &parent_rect);
    int x = parent_rect.left + (parent_rect.right - parent_rect.left - dlgW) / 2;
    int y = parent_rect.top + (parent_rect.bottom - parent_rect.top - dlgH) / 2;

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kSettingsClassName,
        L"Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, dlgW, dlgH,
        parent, nullptr, hinst, &dlg_data
    );

    if (dlg) {
        ShowWindow(dlg, SW_SHOW);
        UpdateWindow(dlg);
    }
}

// ============================================================
// Popup window procedure
// ============================================================

namespace {

LRESULT CALLBACK popup_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        draw_popup(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;  // prevent flicker; WM_PAINT fills everything

    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) {
            hide_popup(hwnd);
        }
        return 0;

    case WM_MOUSEMOVE: {
        int mx = LOWORD(lp);
        int my = HIWORD(lp);
        POINT pt = {mx, my};
        if (PtInRect(&s_chart_area, pt)) {
            s_hover_point = pt;
        } else {
            s_hover_point = std::nullopt;
        }
        InvalidateRect(hwnd, &s_chart_area, FALSE);

        // Track mouse leave to clear hover
        TRACKMOUSEEVENT tme = {};
        tme.cbSize    = sizeof(tme);
        tme.dwFlags   = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (s_hover_point) {
            s_hover_point = std::nullopt;
            InvalidateRect(hwnd, &s_chart_area, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        int mx = LOWORD(lp);
        int my = HIWORD(lp);
        POINT pt = {mx, my};

        // Sign In button
        if (!s_state.is_authenticated && !s_state.is_awaiting_code) {
            if (PtInRect(&s_sign_in_btn, pt)) {
                auto url = usage_service::start_oauth_flow(s_state);
                auto url_w = string_utils::utf8_to_wide(url);
                ShellExecuteW(nullptr, L"open", url_w.c_str(),
                              nullptr, nullptr, SW_SHOWNORMAL);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

        // Code submit button — read from native EDIT control
        if (s_state.is_awaiting_code && PtInRect(&s_submit_btn, pt)) {
            if (s_code_edit) {
                wchar_t code_buf[512] = {};
                GetWindowTextW(s_code_edit, code_buf, 512);
                auto code_utf8 = string_utils::wide_to_utf8(code_buf);
                usage_service::submit_oauth_code(
                    s_state, code_utf8, http_client::winhttp_request);
                SetWindowTextW(s_code_edit, L"");
                ShowWindow(s_code_edit, SW_HIDE);

                // After successful OAuth, immediately fetch usage data
                if (s_state.is_authenticated) {
                    usage_service::fetch_usage(
                        s_state, http_client::winhttp_request);
                    history::record_data_point(
                        s_history, s_state.pct_5h(), s_state.pct_7d());
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Time range buttons
        for (int i = 0; i < static_cast<int>(s_range_buttons.size()); ++i) {
            if (PtInRect(&s_range_buttons[static_cast<size_t>(i)], pt)) {
                s_selected_range = time_range_from_index(i);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

        // Refresh — fetch usage data directly
        if (PtInRect(&s_refresh_btn, pt)) {
            usage_service::fetch_usage(
                s_state, http_client::winhttp_request);
            if (s_state.is_authenticated) {
                history::record_data_point(
                    s_history, s_state.pct_5h(), s_state.pct_7d());
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Settings
        if (PtInRect(&s_settings_btn, pt)) {
            static notification::NotificationState s_notif_state;
            show_settings_dialog(hwnd, s_state, s_notif_state);
            return 0;
        }

        return 0;
    }

    case WM_CTLCOLOREDIT: {
        // Dark theme for the EDIT control
        auto hdc_edit = reinterpret_cast<HDC>(wp);
        SetTextColor(hdc_edit, RGB(240, 240, 240));
        SetBkColor(hdc_edit, RGB(50, 50, 50));
        static HBRUSH s_edit_brush = CreateSolidBrush(RGB(50, 50, 50));
        return reinterpret_cast<LRESULT>(s_edit_brush);
    }

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ============================================================
// Drawing implementation
// ============================================================

void draw_popup(HWND hwnd, HDC hdc) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int width  = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    // Double-buffered GDI+ drawing
    Gdiplus::Bitmap buffer(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&buffer);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // Background
    g.Clear(bg_color());

    int y = kMargin;

    draw_header(g, y);
    y += kSectionGap / 2;

    // Hide EDIT control when not in code entry mode
    if (!s_state.is_awaiting_code && s_code_edit) {
        ShowWindow(s_code_edit, SW_HIDE);
    }

    if (!s_state.is_authenticated) {
        if (s_state.is_awaiting_code) {
            draw_code_entry(g, y);
        } else {
            draw_sign_in(g, y);
        }
    } else {
        draw_usage_bars(g, y);

        if (s_state.usage && s_state.usage->extra_usage &&
            s_state.usage->extra_usage->is_enabled) {
            y += 4;
            draw_divider(g, y);
            y += kSectionGap;
            draw_extra_usage(g, y);
        }

        y += 4;
        draw_divider(g, y);
        y += kSectionGap;

        draw_chart(g, y);

        if (s_state.last_error) {
            y += 4;
            draw_divider(g, y);
            y += kSectionGap;
            draw_error(g, y);
        }

        y += 4;
        draw_divider(g, y);
        y += kSectionGap;
    }

    draw_footer(g, y);

    // Transfer to screen
    Gdiplus::Graphics screen(hdc);
    screen.DrawImage(&buffer, 0, 0);
}

void draw_header(Gdiplus::Graphics& g, int& y) {
    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font font(&family, kFontSizeHeader, Gdiplus::FontStyleBold,
                       Gdiplus::UnitPoint);
    draw_text_left(g, L"Claude Usage", font, text_white(), kMargin, y,
                   kPopupWidth - kMargin * 2);
    y += kHeaderHeight;

    // Divider
    draw_divider(g, y);
    y += kSectionGap;
}

void draw_sign_in(Gdiplus::Graphics& g, int& y) {
    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font font(&family, kFontSizeLabel, Gdiplus::FontStyleRegular,
                       Gdiplus::UnitPoint);

    draw_text_left(g, L"Sign in to view your usage.", font, text_secondary(),
                   kMargin, y, kPopupWidth - kMargin * 2);
    y += 24;

    // Sign In button
    int btn_w = kPopupWidth - kMargin * 2;
    int btn_h = 30;
    s_sign_in_btn = {kMargin, y, kMargin + btn_w, y + btn_h};

    Gdiplus::SolidBrush btn_brush(btn_primary());
    draw_rounded_rect(g, btn_brush, kMargin, y, btn_w, btn_h, 4);

    Gdiplus::Font btn_font(&family, kFontSizeButton, Gdiplus::FontStyleBold,
                           Gdiplus::UnitPoint);
    draw_text_center(g, L"Sign in with Claude", btn_font, text_white(),
                     kMargin, y, btn_w, btn_h);
    y += btn_h + kSectionGap;

    // Error
    if (s_state.last_error) {
        auto err_w = string_utils::utf8_to_wide(s_state.last_error.value());
        Gdiplus::Font small_font(&family, kFontSizeSmall,
                                 Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
        draw_text_left(g, err_w, small_font, text_error(),
                       kMargin, y, kPopupWidth - kMargin * 2);
        y += 16;
    }

    draw_divider(g, y);
    y += kSectionGap;
}

void draw_code_entry(Gdiplus::Graphics& g, int& y) {
    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font font(&family, kFontSizeLabel, Gdiplus::FontStyleRegular,
                       Gdiplus::UnitPoint);

    draw_text_left(g, L"Paste the code from your browser:", font,
                   text_secondary(), kMargin, y, kPopupWidth - kMargin * 2);
    y += 26;

    // Position the native EDIT control and show it
    int edit_w = kPopupWidth - kMargin * 2;
    int edit_h = 30;
    if (s_code_edit) {
        SetWindowPos(s_code_edit, nullptr, kMargin, y, edit_w, edit_h,
                     SWP_NOZORDER);
        ShowWindow(s_code_edit, SW_SHOW);
        // Leave a gap for the EDIT control (GDI+ draws around it)
    }
    y += edit_h + 10;

    // Submit / Cancel buttons
    int half_w = (kPopupWidth - kMargin * 2 - 8) / 2;

    // Cancel
    Gdiplus::SolidBrush cancel_bg(btn_bg());
    draw_rounded_rect(g, cancel_bg, kMargin, y, half_w, kBtnHeight, 4);
    Gdiplus::Font btn_font(&family, kFontSizeButton,
                           Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    draw_text_center(g, L"Cancel", btn_font, text_white(),
                     kMargin, y, half_w, kBtnHeight);

    // Submit
    int submit_x = kMargin + half_w + 8;
    s_submit_btn = {submit_x, y, submit_x + half_w, y + kBtnHeight};
    Gdiplus::SolidBrush submit_bg(btn_primary());
    draw_rounded_rect(g, submit_bg, submit_x, y, half_w, kBtnHeight, 4);
    draw_text_center(g, L"Submit", btn_font, text_white(),
                     submit_x, y, half_w, kBtnHeight);

    y += kBtnHeight + kSectionGap;
}

void draw_usage_bars(Gdiplus::Graphics& g, int& y) {
    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font label_font(&family, kFontSizeLabel,
                             Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    Gdiplus::Font small_font(&family, kFontSizeSmall,
                             Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);

    int content_w = kPopupWidth - kMargin * 2;

    auto draw_bucket = [&](const wchar_t* label,
                           const std::optional<usage_model::UsageBucket>& bucket) {
        double pct = 0.0;
        if (bucket && bucket->utilization) {
            pct = bucket->utilization.value() / 100.0;
        }

        // Label and percentage
        draw_text_left(g, label, label_font, text_white(),
                       kMargin, y, content_w / 2);

        wchar_t pct_str[16];
        if (bucket && bucket->utilization) {
            swprintf(pct_str, 16, L"%d%%",
                     static_cast<int>(std::round(bucket->utilization.value())));
        } else {
            swprintf(pct_str, 16, L"\u2014");  // em-dash
        }
        draw_text_right(g, pct_str, label_font, text_white(),
                        kMargin, y, content_w);
        y += 22;

        // Progress bar
        draw_progress_bar(g, kMargin, y, content_w, kBarHeight,
                          pct, color_for_pct(pct));
        y += kBarHeight + 4;

        // Reset time
        if (bucket && bucket->resets_at) {
            auto reset_tp = usage_model::resets_at_date(*bucket);
            if (reset_tp) {
                auto reset_text = L"resets " + format_reset_time(reset_tp.value());
                draw_text_left(g, reset_text, small_font, text_secondary(),
                               kMargin, y, content_w);
            }
        }
        y += 20;
    };

    if (s_state.usage) {
        draw_bucket(L"5-Hour Window", s_state.usage->five_hour);
        draw_bucket(L"7-Day Window", s_state.usage->seven_day);

        // Per-model (if available)
        if (s_state.usage->seven_day_opus &&
            s_state.usage->seven_day_opus->utilization) {
            y += 2;
            draw_divider(g, y);
            y += kSectionGap;

            draw_text_left(g, L"Per-Model (7 day)", small_font,
                           text_secondary(), kMargin, y, content_w);
            y += 16;

            draw_bucket(L"Opus", s_state.usage->seven_day_opus);
            if (s_state.usage->seven_day_sonnet) {
                draw_bucket(L"Sonnet", s_state.usage->seven_day_sonnet);
            }
        }
    } else {
        draw_text_left(g, L"Loading...", label_font, text_secondary(),
                       kMargin, y, content_w);
        y += 20;
    }
}

void draw_extra_usage(Gdiplus::Graphics& g, int& y) {
    if (!s_state.usage || !s_state.usage->extra_usage) return;

    const auto& extra = s_state.usage->extra_usage.value();
    int content_w = kPopupWidth - kMargin * 2;

    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font label_font(&family, kFontSizeLabel,
                             Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    Gdiplus::Font small_font(&family, kFontSizeSmall,
                             Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);

    draw_text_left(g, L"Extra Usage", label_font, text_white(),
                   kMargin, y, content_w);
    y += 24;

    auto used = usage_model::used_credits_amount(extra);
    auto limit = usage_model::monthly_limit_amount(extra);
    if (used && limit) {
        auto used_str = usage_model::format_usd(used.value());
        auto limit_str = usage_model::format_usd(limit.value());
        auto text = string_utils::utf8_to_wide(used_str + " / " + limit_str);

        draw_text_left(g, text, small_font, text_secondary(),
                       kMargin, y, content_w / 2);

        if (extra.utilization) {
            wchar_t pct_str[16];
            swprintf(pct_str, 16, L"%d%%",
                     static_cast<int>(std::round(extra.utilization.value())));
            draw_text_right(g, pct_str, small_font, text_secondary(),
                            kMargin, y, content_w);
        }
        y += 16;

        double pct = extra.utilization.value_or(0.0) / 100.0;
        draw_progress_bar(g, kMargin, y, content_w, kBarHeight,
                          pct, bar_blue());
        y += kBarHeight + 4;
    }
}

void draw_chart(Gdiplus::Graphics& g, int& y) {
    int content_w = kPopupWidth - kMargin * 2;

    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font small_font(&family, kFontSizeSmall,
                             Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    Gdiplus::Font btn_font(&family, kFontSizeButton,
                           Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);

    // Time range selector buttons
    s_range_buttons.clear();
    int total_btn_w = kRangeButtonW * kTimeRangeCount + 4 * (kTimeRangeCount - 1);
    int btn_x = kMargin + (content_w - total_btn_w) / 2;

    for (int i = 0; i < kTimeRangeCount; ++i) {
        auto range = time_range_from_index(i);
        RECT btn_rc = {btn_x, y, btn_x + kRangeButtonW, y + kRangeButtonH};
        s_range_buttons.push_back(btn_rc);

        if (range == s_selected_range) {
            Gdiplus::SolidBrush sel_bg(range_sel_bg());
            draw_rounded_rect(g, sel_bg, btn_x, y, kRangeButtonW, kRangeButtonH, 3);
        }

        draw_text_center(g, time_range_label_w(range), btn_font,
                         range == s_selected_range ? text_white() : text_secondary(),
                         btn_x, y, kRangeButtonW, kRangeButtonH);

        btn_x += kRangeButtonW + 4;
    }
    y += kRangeButtonH + 8;

    // Chart area
    int chart_x = kMargin;
    int chart_y = y;
    int chart_w = content_w;
    int chart_h = kChartHeight;
    s_chart_area = {chart_x, chart_y, chart_x + chart_w, chart_y + chart_h};

    // Get downsampled points
    auto points = history::downsample_points(
        s_history.data_points, s_selected_range);

    if (points.empty()) {
        draw_text_center(g, L"No history data yet.", small_font,
                         text_secondary(), chart_x, chart_y, chart_w, chart_h);
        y += chart_h + 4;

        // Legend
        Gdiplus::SolidBrush blue_brush(chart_5h());
        Gdiplus::SolidBrush orange_brush(chart_7d());
        g.FillEllipse(&blue_brush, kMargin, y, 8, 8);
        draw_text_left(g, L"5h", small_font, text_secondary(),
                       kMargin + 12, y - 1, 30);
        g.FillEllipse(&orange_brush, kMargin + 50, y, 8, 8);
        draw_text_left(g, L"7d", small_font, text_secondary(),
                       kMargin + 62, y - 1, 30);
        y += 14;
        return;
    }

    // Y-axis grid lines: 0%, 25%, 50%, 75%, 100%
    Gdiplus::Pen grid_pen(grid_line_color(), 1.0f);
    for (int pct : {25, 50, 75}) {
        int gy = chart_y + chart_h - (chart_h * pct / 100);
        g.DrawLine(&grid_pen, chart_x, gy, chart_x + chart_w, gy);

        wchar_t label[8];
        swprintf(label, 8, L"%d%%", pct);
        Gdiplus::Font axis_font(&family, 7.0f, Gdiplus::FontStyleRegular,
                                Gdiplus::UnitPoint);
        draw_text_left(g, label, axis_font, text_secondary(),
                       chart_x + 2, gy - 10, 30);
    }

    // Determine time range for x-axis mapping
    auto now = std::chrono::system_clock::now();
    auto range_sec = usage_model::time_range_interval(s_selected_range);
    auto range_start = now - range_sec;

    auto time_to_x = [&](std::chrono::system_clock::time_point t) -> float {
        using namespace std::chrono;
        auto elapsed = duration_cast<duration<double>>(t - range_start).count();
        auto total   = duration_cast<duration<double>>(range_sec).count();
        if (total <= 0.0) return static_cast<float>(chart_x);
        double ratio = elapsed / total;
        return static_cast<float>(chart_x) +
               static_cast<float>(ratio) * static_cast<float>(chart_w);
    };

    auto pct_to_y = [&](double pct) -> float {
        double clamped = (std::min)(1.0, (std::max)(0.0, pct));
        return static_cast<float>(chart_y + chart_h) -
               static_cast<float>(clamped) * static_cast<float>(chart_h);
    };

    // Draw 5h line
    if (points.size() >= 2) {
        std::vector<Gdiplus::PointF> pts_5h;
        std::vector<Gdiplus::PointF> pts_7d;
        pts_5h.reserve(points.size());
        pts_7d.reserve(points.size());

        for (const auto& dp : points) {
            float px = time_to_x(dp.timestamp);
            pts_5h.push_back({px, pct_to_y(dp.pct_5h)});
            pts_7d.push_back({px, pct_to_y(dp.pct_7d)});
        }

        Gdiplus::Pen pen_5h(chart_5h(), 2.0f);
        Gdiplus::Pen pen_7d(chart_7d(), 2.0f);

        if (pts_5h.size() >= 2) {
            g.DrawCurve(&pen_5h, pts_5h.data(),
                        static_cast<INT>(pts_5h.size()));
        }
        if (pts_7d.size() >= 2) {
            g.DrawCurve(&pen_7d, pts_7d.data(),
                        static_cast<INT>(pts_7d.size()));
        }
    }

    // Hover
    if (s_hover_point) {
        draw_chart_hover(g, points);
    }

    y += chart_h + 4;

    // Legend
    Gdiplus::SolidBrush blue_brush(chart_5h());
    Gdiplus::SolidBrush orange_brush(chart_7d());
    g.FillEllipse(&blue_brush, kMargin, y, 8, 8);
    draw_text_left(g, L"5h", small_font, text_secondary(),
                   kMargin + 12, y - 1, 30);
    g.FillEllipse(&orange_brush, kMargin + 50, y, 8, 8);
    draw_text_left(g, L"7d", small_font, text_secondary(),
                   kMargin + 62, y - 1, 30);
    y += 14;
}

void draw_chart_hover(Gdiplus::Graphics& g,
                      const std::vector<usage_model::UsageDataPoint>& points)
{
    if (!s_hover_point || points.empty()) return;

    int chart_x = s_chart_area.left;
    int chart_w = s_chart_area.right - s_chart_area.left;
    int chart_y = s_chart_area.top;
    int chart_h = s_chart_area.bottom - s_chart_area.top;

    // Map hover X to a date
    auto now = std::chrono::system_clock::now();
    auto range_sec = usage_model::time_range_interval(s_selected_range);
    auto range_start = now - range_sec;

    double ratio = 0.0;
    if (chart_w > 0) {
        ratio = static_cast<double>(s_hover_point->x - chart_x) /
                static_cast<double>(chart_w);
    }
    ratio = (std::min)(1.0, (std::max)(0.0, ratio));

    using namespace std::chrono;
    auto total_sec = duration_cast<duration<double>>(range_sec).count();
    auto hover_time = range_start + duration_cast<system_clock::duration>(
        duration<double>(ratio * total_sec));

    auto interp = history::interpolate_values(hover_time, points);
    if (!interp) return;

    // Vertical rule line
    float vx = static_cast<float>(s_hover_point->x);
    Gdiplus::Pen rule_pen(Gdiplus::Color(100, 200, 200, 200), 1.0f);
    g.DrawLine(&rule_pen, vx, static_cast<float>(chart_y),
               vx, static_cast<float>(chart_y + chart_h));

    // Point markers
    double pct_5h_clamped = (std::min)(1.0, (std::max)(0.0, interp->pct_5h));
    double pct_7d_clamped = (std::min)(1.0, (std::max)(0.0, interp->pct_7d));
    float y_5h = static_cast<float>(chart_y + chart_h) -
                 static_cast<float>(pct_5h_clamped) * static_cast<float>(chart_h);
    float y_7d = static_cast<float>(chart_y + chart_h) -
                 static_cast<float>(pct_7d_clamped) * static_cast<float>(chart_h);

    Gdiplus::SolidBrush blue_fill(chart_5h());
    Gdiplus::SolidBrush orange_fill(chart_7d());
    g.FillEllipse(&blue_fill, vx - 4.0f, y_5h - 4.0f, 8.0f, 8.0f);
    g.FillEllipse(&orange_fill, vx - 4.0f, y_7d - 4.0f, 8.0f, 8.0f);

    // Tooltip
    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font tip_font(&family, 8.0f, Gdiplus::FontStyleRegular,
                           Gdiplus::UnitPoint);

    wchar_t tip_text[64];
    swprintf(tip_text, 64, L"5h: %d%%  7d: %d%%",
             static_cast<int>(std::round(interp->pct_5h * 100.0)),
             static_cast<int>(std::round(interp->pct_7d * 100.0)));

    int tip_w = 110;
    int tip_h = 20;
    int tip_x = s_hover_point->x - tip_w / 2;
    int tip_y = chart_y - tip_h - 4;

    // Clamp tooltip within chart area
    if (tip_x < chart_x) tip_x = chart_x;
    if (tip_x + tip_w > chart_x + chart_w) tip_x = chart_x + chart_w - tip_w;

    Gdiplus::SolidBrush tip_bg_brush(tooltip_bg());
    draw_rounded_rect(g, tip_bg_brush, tip_x, tip_y, tip_w, tip_h, 4);
    draw_text_center(g, tip_text, tip_font, text_white(),
                     tip_x, tip_y, tip_w, tip_h);
}

void draw_error(Gdiplus::Graphics& g, int& y) {
    if (!s_state.last_error) return;

    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font font(&family, kFontSizeSmall, Gdiplus::FontStyleRegular,
                       Gdiplus::UnitPoint);

    auto err_w = string_utils::utf8_to_wide(s_state.last_error.value());
    draw_text_left(g, err_w, font, text_error(),
                   kMargin, y, kPopupWidth - kMargin * 2);
    y += 16;
}

void draw_footer(Gdiplus::Graphics& g, int& y) {
    int content_w = kPopupWidth - kMargin * 2;

    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font small_font(&family, kFontSizeSmall,
                             Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    Gdiplus::Font btn_font(&family, kFontSizeButton,
                           Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);

    // "Updated X ago"
    if (s_state.last_updated) {
        auto text = L"Updated " + format_relative_time(s_state.last_updated.value()) +
                    L" ago";
        draw_text_left(g, text, small_font, text_secondary(),
                       kMargin, y, content_w);
    }
    y += 16;

    // Refresh and Settings buttons
    int btn_w = 65;
    int btn_h = kBtnHeight;

    // Refresh
    int refresh_x = kPopupWidth - kMargin - btn_w;
    s_refresh_btn = {refresh_x, y, refresh_x + btn_w, y + btn_h};
    Gdiplus::SolidBrush refresh_bg(btn_bg());
    draw_rounded_rect(g, refresh_bg, refresh_x, y, btn_w, btn_h, 4);
    draw_text_center(g, L"Refresh", btn_font, text_white(),
                     refresh_x, y, btn_w, btn_h);

    // Settings
    int settings_x = refresh_x - btn_w - 8;
    s_settings_btn = {settings_x, y, settings_x + btn_w, y + btn_h};
    Gdiplus::SolidBrush settings_bg(btn_bg());
    draw_rounded_rect(g, settings_bg, settings_x, y, btn_w, btn_h, 4);
    draw_text_center(g, L"Settings", btn_font, text_white(),
                     settings_x, y, btn_w, btn_h);
    y += btn_h + 6;

    // Account email
    if (s_state.is_authenticated && s_state.account_email) {
        auto email_w = string_utils::utf8_to_wide(s_state.account_email.value());
        draw_text_left(g, email_w, small_font, text_secondary(),
                       kMargin, y, content_w);
        y += 14;
    }
}

void draw_divider(Gdiplus::Graphics& g, int y) {
    Gdiplus::Pen pen(divider_color(), 1.0f);
    g.DrawLine(&pen, kMargin, y, kPopupWidth - kMargin, y);
}

// ============================================================
// Drawing utility functions
// ============================================================

void draw_progress_bar(Gdiplus::Graphics& g, int x, int y, int w, int h,
                       double pct, Gdiplus::Color fill)
{
    // Background
    Gdiplus::SolidBrush bg_brush(bar_bg());
    draw_rounded_rect(g, bg_brush, x, y, w, h, h / 2);

    // Foreground
    int fill_w = static_cast<int>(
        static_cast<double>(w) * (std::min)(1.0, (std::max)(0.0, pct)));
    if (fill_w > 0) {
        Gdiplus::SolidBrush fill_brush(fill);
        draw_rounded_rect(g, fill_brush, x, y, fill_w, h, h / 2);
    }
}

void draw_rounded_rect(Gdiplus::Graphics& g, Gdiplus::Brush& brush,
                       int x, int y, int w, int h, int radius)
{
    if (radius <= 0) {
        g.FillRectangle(&brush, x, y, w, h);
        return;
    }

    Gdiplus::GraphicsPath path;
    int d = radius * 2;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
    g.FillPath(&brush, &path);
}

void draw_rounded_rect_outline(Gdiplus::Graphics& g, Gdiplus::Pen& pen,
                               int x, int y, int w, int h, int radius)
{
    if (radius <= 0) {
        g.DrawRectangle(&pen, x, y, w, h);
        return;
    }

    Gdiplus::GraphicsPath path;
    int d = radius * 2;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
    g.DrawPath(&pen, &path);
}

void draw_text_left(Gdiplus::Graphics& g, const std::wstring& text,
                    const Gdiplus::Font& font, Gdiplus::Color color,
                    int x, int y, int w)
{
    Gdiplus::SolidBrush brush(color);
    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(Gdiplus::StringAlignmentNear);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentNear);
    fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    Gdiplus::RectF rc(static_cast<float>(x), static_cast<float>(y),
                      static_cast<float>(w), 200.0f);
    g.DrawString(text.c_str(), static_cast<INT>(text.length()),
                 &font, rc, &fmt, &brush);
}

void draw_text_right(Gdiplus::Graphics& g, const std::wstring& text,
                     const Gdiplus::Font& font, Gdiplus::Color color,
                     int x, int y, int w)
{
    Gdiplus::SolidBrush brush(color);
    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(Gdiplus::StringAlignmentFar);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentNear);
    Gdiplus::RectF rc(static_cast<float>(x), static_cast<float>(y),
                      static_cast<float>(w), 200.0f);
    g.DrawString(text.c_str(), static_cast<INT>(text.length()),
                 &font, rc, &fmt, &brush);
}

void draw_text_center(Gdiplus::Graphics& g, const std::wstring& text,
                      const Gdiplus::Font& font, Gdiplus::Color color,
                      int x, int y, int w, int h)
{
    Gdiplus::SolidBrush brush(color);
    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::RectF rc(static_cast<float>(x), static_cast<float>(y),
                      static_cast<float>(w), static_cast<float>(h));
    g.DrawString(text.c_str(), static_cast<INT>(text.length()),
                 &font, rc, &fmt, &brush);
}

// ============================================================
// Time formatting helpers
// ============================================================

std::wstring format_relative_time(std::chrono::system_clock::time_point then) {
    using namespace std::chrono;
    auto elapsed = duration_cast<seconds>(system_clock::now() - then).count();

    if (elapsed < 60) return L"just now";
    if (elapsed < 3600) {
        auto mins = elapsed / 60;
        return std::to_wstring(mins) + L"m";
    }
    auto hours = elapsed / 3600;
    return std::to_wstring(hours) + L"h";
}

std::wstring format_reset_time(std::chrono::system_clock::time_point reset) {
    using namespace std::chrono;
    auto remaining = duration_cast<seconds>(reset - system_clock::now()).count();

    if (remaining <= 0) return L"soon";
    if (remaining < 3600) {
        auto mins = remaining / 60;
        return L"in " + std::to_wstring(mins) + L"m";
    }
    auto hours = remaining / 3600;
    auto mins  = (remaining % 3600) / 60;
    if (hours >= 24) {
        auto days = hours / 24;
        hours = hours % 24;
        return L"in " + std::to_wstring(days) + L"d " +
               std::to_wstring(hours) + L"h";
    }
    return L"in " + std::to_wstring(hours) + L"h " +
           std::to_wstring(mins) + L"m";
}

}  // anonymous namespace

}  // namespace popup
