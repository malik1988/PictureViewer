#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <objbase.h>
#include <gdiplus.h>
#include <wincodec.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wrl/client.h>

// SDK 中这两个宏位于 windowsx.h，按需在此定义以免引入整套消息分发宏。
#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace pv {

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// GDI+ 生命周期：进程级单例，放在 wWinMain 的栈上即可。
// ---------------------------------------------------------------------------
class GdiplusLifetime {
public:
    GdiplusLifetime() noexcept {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&token_, &input, nullptr);
    }
    ~GdiplusLifetime() {
        if (token_) {
            Gdiplus::GdiplusShutdown(token_);
        }
    }
    GdiplusLifetime(const GdiplusLifetime&) = delete;
    GdiplusLifetime& operator=(const GdiplusLifetime&) = delete;

private:
    ULONG_PTR token_ = 0;
};

// ---------------------------------------------------------------------------
// COM 生命周期
// ---------------------------------------------------------------------------
class ComLifetime {
public:
    ComLifetime() noexcept { hr_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
    ~ComLifetime() { if (SUCCEEDED(hr_)) CoUninitialize(); }
    ComLifetime(const ComLifetime&) = delete;
    ComLifetime& operator=(const ComLifetime&) = delete;
    bool ok() const noexcept { return SUCCEEDED(hr_); }

private:
    HRESULT hr_ = S_FALSE;
};

// ---------------------------------------------------------------------------
// GDI 对象 RAII：HBITMAP / HBRUSH / HPEN / HFONT / HRGN ...
// ---------------------------------------------------------------------------
class ScopedGdiObject {
public:
    explicit ScopedGdiObject(HGDIOBJ obj = nullptr) noexcept : obj_(obj) {}
    ~ScopedGdiObject() { reset(); }

    ScopedGdiObject(const ScopedGdiObject&) = delete;
    ScopedGdiObject& operator=(const ScopedGdiObject&) = delete;

    ScopedGdiObject(ScopedGdiObject&& other) noexcept : obj_(other.release()) {}
    ScopedGdiObject& operator=(ScopedGdiObject&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    void reset(HGDIOBJ obj = nullptr) noexcept {
        if (obj_ && obj_ != obj) {
            DeleteObject(obj_);
        }
        obj_ = obj;
    }
    HGDIOBJ get() const noexcept { return obj_; }
    HGDIOBJ release() noexcept {
        HGDIOBJ tmp = obj_;
        obj_ = nullptr;
        return tmp;
    }
    explicit operator bool() const noexcept { return obj_ != nullptr; }

private:
    HGDIOBJ obj_ = nullptr;
};

// ---------------------------------------------------------------------------
// HDC RAII
// ---------------------------------------------------------------------------
class ScopedWindowDc {
public:
    explicit ScopedWindowDc(HWND hwnd) noexcept
        : hwnd_(hwnd), dc_(hwnd ? GetDC(hwnd) : nullptr) {}
    ~ScopedWindowDc() { if (dc_ && hwnd_) ReleaseDC(hwnd_, dc_); }
    ScopedWindowDc(const ScopedWindowDc&) = delete;
    ScopedWindowDc& operator=(const ScopedWindowDc&) = delete;
    HDC get() const noexcept { return dc_; }
    explicit operator bool() const noexcept { return dc_ != nullptr; }

private:
    HWND hwnd_ = nullptr;
    HDC dc_ = nullptr;
};

class ScopedMemDc {
public:
    explicit ScopedMemDc(HDC ref = nullptr) noexcept : dc_(CreateCompatibleDC(ref)) {}
    ~ScopedMemDc() { if (dc_) DeleteDC(dc_); }
    ScopedMemDc(const ScopedMemDc&) = delete;
    ScopedMemDc& operator=(const ScopedMemDc&) = delete;
    ScopedMemDc(ScopedMemDc&& other) noexcept : dc_(other.release()) {}
    ScopedMemDc& operator=(ScopedMemDc&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }
    void reset(HDC dc = nullptr) noexcept {
        if (dc_ && dc_ != dc) {
            DeleteDC(dc_);
        }
        dc_ = dc;
    }
    HDC get() const noexcept { return dc_; }
    HDC release() noexcept {
        HDC tmp = dc_;
        dc_ = nullptr;
        return tmp;
    }
    explicit operator bool() const noexcept { return dc_ != nullptr; }

private:
    HDC dc_ = nullptr;
};

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------
inline double ClampD(double v, double lo, double hi) noexcept {
    return (std::max)(lo, (std::min)(v, hi));
}

inline std::wstring FormatBytes(ULONGLONG bytes) {
    static const wchar_t* kUnits[] = { L"B", L"kB", L"MB", L"GB", L"TB" };
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    wchar_t buffer[64];
    std::swprintf(buffer, 64, L"%.1f%ls", value, kUnits[unit]);
    return buffer;
}

inline std::wstring Upper(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towupper(c));
    });
    return s;
}

inline std::wstring ExtensionOf(const std::wstring& path) {
    const wchar_t* ext = ::PathFindExtensionW(path.c_str());
    if (!ext || *ext != L'.') {
        return {};
    }
    return Upper(ext + 1);
}

} // namespace pv
