// UniForge.exe entry point. Normally started by Launch.exe with --launch-t0=<QPC>; running it
// directly also works (then only the UniForge.exe launch-to-ready number is available).

#include <windows.h>

#include <exception>
#include <string>

#include "app/App.hpp"
#include "app/platform/Text.hpp"

int WINAPI wWinMain(HINSTANCE /*instance*/, HINSTANCE /*prev*/, PWSTR /*cmdLine*/, int /*show*/) {
    try {
        uniforge::App app;
        return app.run();
    } catch (const std::exception& e) {
        const std::wstring text = L"UniForge stopped because of an unexpected error:\n\n" +
                                  uniforge::platform::toWide(e.what());
        MessageBoxW(nullptr, text.c_str(), L"UniForge", MB_OK | MB_ICONERROR);
        return 2;
    }
}
