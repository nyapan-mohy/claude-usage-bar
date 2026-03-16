#pragma once

// RAII wrappers for Win32 handles.
//
// Usage:
//   auto icon = win32_raii::make_icon(CreateIcon(...));
//   // icon is automatically destroyed when it goes out of scope
//
//   auto dc = win32_raii::UniqueDc(CreateCompatibleDC(nullptr));
//   // dc is automatically deleted when it goes out of scope

#include <windows.h>
#include <winhttp.h>
#include <memory>

// GDI+ headers require global min/max, which NOMINMAX removes.
// Provide them from <algorithm> before including GDI+ headers.
#include <algorithm>
using std::min;
using std::max;
#include <objidl.h>
#include <gdiplus.h>

namespace win32_raii {

// --- GDI objects (HBITMAP, HFONT, HBRUSH, HPEN, etc.) ---
// All GDI objects are released with DeleteObject.

struct GdiObjectDeleter {
    void operator()(HGDIOBJ obj) const noexcept {
        if (obj) ::DeleteObject(obj);
    }
};

template<typename HandleType>
using UniqueGdiObject = std::unique_ptr<std::remove_pointer_t<HandleType>, GdiObjectDeleter>;

using UniqueBitmap = UniqueGdiObject<HBITMAP>;
using UniqueFont   = UniqueGdiObject<HFONT>;
using UniqueBrush  = UniqueGdiObject<HBRUSH>;
using UniquePen    = UniqueGdiObject<HPEN>;

// --- HICON ---

struct IconDeleter {
    void operator()(HICON icon) const noexcept {
        if (icon) ::DestroyIcon(icon);
    }
};

using UniqueIcon = std::unique_ptr<std::remove_pointer_t<HICON>, IconDeleter>;

inline UniqueIcon make_icon(HICON h) {
    return UniqueIcon(h);
}

// --- HDC (CreateDC / CreateCompatibleDC) ---

struct DcDeleter {
    void operator()(HDC dc) const noexcept {
        if (dc) ::DeleteDC(dc);
    }
};

using UniqueDc = std::unique_ptr<std::remove_pointer_t<HDC>, DcDeleter>;

// --- WinHTTP handle (HINTERNET) ---

struct HInternetDeleter {
    void operator()(HINTERNET h) const noexcept {
        if (h) ::WinHttpCloseHandle(h);
    }
};

using UniqueHInternet = std::unique_ptr<void, HInternetDeleter>;

inline UniqueHInternet make_hinternet(HINTERNET h) {
    return UniqueHInternet(h);
}

// --- GDI+ initialization scope ---
// Create one instance in WinMain; GDI+ is available until it goes out of scope.

struct GdiplusScope {
    ULONG_PTR token = 0;

    GdiplusScope() {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&token, &input, nullptr);
    }

    ~GdiplusScope() {
        if (token) Gdiplus::GdiplusShutdown(token);
    }

    GdiplusScope(const GdiplusScope&) = delete;
    GdiplusScope& operator=(const GdiplusScope&) = delete;
};

}  // namespace win32_raii
