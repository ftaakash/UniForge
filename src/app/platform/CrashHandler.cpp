#include "app/platform/CrashHandler.hpp"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

#include "app/BuildInfo.hpp"

namespace uniforge::platform {

namespace {

// Fixed storage: the handler runs on a damaged process, so it allocates nothing.
wchar_t g_reportPath[MAX_PATH * 2] = {};
constexpr DWORD kSimulatedCrashCode = 0xE0554601; // 'UF' + 1, customer bit set

void writeReport(const char* what, DWORD code, const void* address) {
    FILE* f = nullptr;
    if (g_reportPath[0] != L'\0' && _wfopen_s(&f, g_reportPath, L"w") == 0 && f) {
        SYSTEMTIME now{};
        GetSystemTime(&now);
        std::fprintf(f,
                     "UniForge %s (commit %s) crashed\n"
                     "time (UTC): %04u-%02u-%02uT%02u:%02u:%02uZ\n"
                     "reason: %s\n"
                     "exception code: 0x%08lX\n"
                     "address: %p\n",
                     build::kVersion, build::kGitCommit, now.wYear, now.wMonth, now.wDay,
                     now.wHour, now.wMinute, now.wSecond, what, static_cast<unsigned long>(code),
                     address);
        std::fclose(f);
    }
    wchar_t text[MAX_PATH * 2 + 256];
    swprintf_s(text,
               L"UniForge stopped unexpectedly (exception 0x%08lX).\n\n"
               L"A crash report was written to:\n%s\n\n"
               L"Please include it if you report the problem.",
               static_cast<unsigned long>(code),
               g_reportPath[0] != L'\0' ? g_reportPath : L"(no writable log folder)");
    MessageBoxW(nullptr, text, L"UniForge", MB_OK | MB_ICONERROR | MB_TASKMODAL);
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* info) {
    const DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    const void* address =
        info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr;
    writeReport(code == kSimulatedCrashCode ? "simulated crash (--simulate-crash)"
                                            : "unhandled structured exception",
                code, address);
    return EXCEPTION_EXECUTE_HANDLER; // terminate without the Windows Error Reporting dialog
}

void onTerminate() {
    writeReport("std::terminate (uncaught C++ exception on a worker thread, or noexcept violation)",
                0, nullptr);
    std::_Exit(3);
}

} // namespace

void installCrashHandler(const std::filesystem::path& logsDir) {
    const std::wstring path =
        (logsDir / (L"crash_" + std::to_wstring(GetCurrentProcessId()) + L".txt")).wstring();
    if (path.size() < std::size(g_reportPath)) {
        wcscpy_s(g_reportPath, path.c_str());
    }
    SetUnhandledExceptionFilter(onUnhandledException);
    std::set_terminate(onTerminate);
}

void simulateCrash() {
    RaiseException(kSimulatedCrashCode, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    std::abort(); // not reached
}

} // namespace uniforge::platform
