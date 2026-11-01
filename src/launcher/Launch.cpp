// Launch.exe: the one file the evaluator double-clicks (spec §7.1, A5).
//
// It contains no application logic. It records its start time (QueryPerformanceCounter),
// starts app\UniForge.exe next to itself with --launch-t0=<that value> and the package folder
// as the working directory, and exits. If UniForge.exe cannot be started it shows the path it
// tried, the system error text and how to fix it, and exits with a non-zero code.

#include <windows.h>

#include <string>

namespace {

std::wstring executablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD len =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0) {
            return {};
        }
        if (len < buffer.size()) {
            buffer.resize(len);
            return buffer;
        }
        buffer.resize(buffer.size() * 2); // long path: retry with a bigger buffer
    }
}

std::wstring parentDirectory(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring(L".") : path.substr(0, slash);
}

std::wstring systemErrorText(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text = len ? std::wstring(buffer, len) : L"Unknown error.";
    LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) {
        text.pop_back();
    }
    return text + L" (error " + std::to_wstring(code) + L")";
}

int showLaunchError(const std::wstring& target, DWORD code) {
    const std::wstring message = L"UniForge could not be started.\n\n"
                                 L"Path tried:\n" +
                                 target + L"\n\nSystem error:\n" + systemErrorText(code) +
                                 L"\n\nRe-extract the ZIP and keep the folder structure "
                                 L"(Launch.exe next to the app folder).";
    MessageBoxW(nullptr, message.c_str(), L"UniForge Launcher", MB_OK | MB_ICONERROR);
    return 1;
}

} // namespace

int WINAPI wWinMain(HINSTANCE /*instance*/, HINSTANCE /*prev*/, PWSTR /*cmdLine*/, int /*show*/) {
    LARGE_INTEGER t0{};
    QueryPerformanceCounter(&t0); // first thing: start of the launch-to-ready interval

    const std::wstring exe = executablePath();
    if (exe.empty()) {
        return showLaunchError(L"(could not determine the location of Launch.exe)", GetLastError());
    }
    const std::wstring packageDir = parentDirectory(exe);
    const std::wstring target = packageDir + L"\\app\\UniForge.exe";

    // CreateProcessW may modify the command-line buffer, so it must be writable.
    std::wstring commandLine = L"\"" + target + L"\" --launch-t0=" + std::to_wstring(t0.QuadPart);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(target.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        packageDir.c_str(), &si, &pi)) {
        return showLaunchError(target, GetLastError());
    }
    // Let UniForge take the foreground even though Launch.exe exits immediately.
    AllowSetForegroundWindow(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
