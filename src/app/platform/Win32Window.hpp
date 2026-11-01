#pragma once

#include <windows.h>

#include <functional>
#include <string>

namespace uniforge::platform {

// The single top-level application window. Owns the Win32 class registration and message
// pump and turns the messages UniForge cares about (resize, DPI change, light/dark switch)
// into callbacks. Wide-char APIs only, so titles and paths survive non-ASCII text.
class Win32Window {
public:
    struct Callbacks {
        // Given every message first; return true when it was consumed (Dear ImGui input).
        std::function<bool(HWND, UINT, WPARAM, LPARAM)> preTranslate;
        std::function<void(UINT width, UINT height)> onResize; // client size in pixels
        std::function<void(float dpiScale)> onDpiChanged;      // 1.0 = 96 DPI
        std::function<void()> onSystemThemeChanged;            // Windows app mode toggled
    };

    Win32Window() = default;
    ~Win32Window();
    Win32Window(const Win32Window&) = delete;
    Win32Window& operator=(const Win32Window&) = delete;

    // Creates the window sized for the primary monitor's DPI (not shown yet).
    // Returns false and fills `error` on failure.
    bool create(const std::wstring& title, Callbacks callbacks, std::string& error);
    void show();

    // Processes all pending messages. Returns false once WM_QUIT was received.
    bool pumpMessages();

    [[nodiscard]] HWND handle() const noexcept { return hwnd_; }
    [[nodiscard]] float dpiScale() const noexcept { return dpiScale_; }
    [[nodiscard]] bool minimized() const noexcept { return minimized_; }

    // Matches the title bar to the app theme (DWMWA_USE_IMMERSIVE_DARK_MODE).
    void setDarkTitleBar(bool dark);

    // Minimum client size at 100 % scaling (spec §8.6).
    static constexpr int kMinWidth = 1024;
    static constexpr int kMinHeight = 680;

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    Callbacks callbacks_;
    float dpiScale_ = 1.0f;
    bool minimized_ = false;
};

} // namespace uniforge::platform
