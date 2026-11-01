#include "app/platform/Win32Window.hpp"

#include <algorithm>
#include <dwmapi.h>

#include "app/platform/Text.hpp"

namespace uniforge::platform {

namespace {

constexpr wchar_t kClassName[] = L"UniForgeMainWindow";
constexpr int kDefaultWidth = 1280;
constexpr int kDefaultHeight = 800;

// Converts a client-area size to the outer window size for the given style and DPI.
SIZE outerSize(int clientW, int clientH, DWORD style, UINT dpi) {
    RECT r{0, 0, clientW, clientH};
    AdjustWindowRectExForDpi(&r, style, FALSE, 0, dpi);
    return {r.right - r.left, r.bottom - r.top};
}

} // namespace

Win32Window::~Win32Window() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
    }
    if (instance_) {
        UnregisterClassW(kClassName, instance_);
    }
}

bool Win32Window::create(const std::wstring& title, Callbacks callbacks, std::string& error) {
    callbacks_ = std::move(callbacks);
    instance_ = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Win32Window::wndProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
        error = "RegisterClassExW failed: " + lastErrorText();
        return false;
    }

    // Size the window for the primary monitor's DPI and centre it in the work area.
    const POINT origin{0, 0};
    HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(monitor, &mi);
    const UINT dpi = GetDpiForSystem();
    dpiScale_ = static_cast<float>(dpi) / USER_DEFAULT_SCREEN_DPI;

    const DWORD style = WS_OVERLAPPEDWINDOW;
    const RECT work = mi.rcWork;
    const int workW = work.right - work.left;
    const int workH = work.bottom - work.top;
    SIZE size = outerSize(MulDiv(kDefaultWidth, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI),
                          MulDiv(kDefaultHeight, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI),
                          style, dpi);
    size.cx = std::min<LONG>(size.cx, workW);
    size.cy = std::min<LONG>(size.cy, workH);
    const int x = work.left + (workW - size.cx) / 2;
    const int y = work.top + (workH - size.cy) / 2;

    hwnd_ = CreateWindowExW(0, kClassName, title.c_str(), style, x, y, size.cx, size.cy, nullptr,
                            nullptr, instance_, this);
    if (!hwnd_) {
        error = "CreateWindowExW failed: " + lastErrorText();
        return false;
    }
    // The window may have been placed on a monitor with a different DPI than the system one.
    dpiScale_ = static_cast<float>(GetDpiForWindow(hwnd_)) / USER_DEFAULT_SCREEN_DPI;
    return true;
}

void Win32Window::show() {
    ShowWindow(hwnd_, SW_SHOWDEFAULT);
    UpdateWindow(hwnd_);
}

bool Win32Window::pumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

void Win32Window::setDarkTitleBar(bool dark) {
    const BOOL value = dark ? TRUE : FALSE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Windows 10 20H1+); harmless if unsupported.
    DwmSetWindowAttribute(hwnd_, 20, &value, sizeof(value));
}

LRESULT CALLBACK Win32Window::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        auto* self = static_cast<Win32Window*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    if (self->callbacks_.preTranslate && self->callbacks_.preTranslate(hwnd, msg, wParam, lParam)) {
        return 1;
    }
    return self->handleMessage(hwnd, msg, wParam, lParam);
}

LRESULT Win32Window::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        minimized_ = (wParam == SIZE_MINIMIZED);
        if (!minimized_ && callbacks_.onResize) {
            callbacks_.onResize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        const UINT dpi = hwnd_ ? GetDpiForWindow(hwnd_) : USER_DEFAULT_SCREEN_DPI;
        const SIZE minSize =
            outerSize(MulDiv(kMinWidth, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI),
                      MulDiv(kMinHeight, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI),
                      static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE)), dpi);
        info->ptMinTrackSize = {minSize.cx, minSize.cy};
        return 0;
    }

    case WM_DPICHANGED: {
        // Per-monitor v2: Windows suggests a new rect that keeps the physical size.
        dpiScale_ = static_cast<float>(HIWORD(wParam)) / USER_DEFAULT_SCREEN_DPI;
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (callbacks_.onDpiChanged) {
            callbacks_.onDpiChanged(dpiScale_);
        }
        return 0;
    }

    case WM_SETTINGCHANGE:
        if (lParam && callbacks_.onSystemThemeChanged &&
            lstrcmpW(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0) {
            callbacks_.onSystemThemeChanged();
        }
        break;

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) { // no Alt menu: Alt is used by ImGui shortcuts
            return 0;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        hwnd_ = nullptr;
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace uniforge::platform
