#include "cli/platform/Process.hpp"

#include <windows.h>

#include <intrin.h>
#include <psapi.h> // K32GetProcessMemoryInfo lives in kernel32 (PSAPI_VERSION 2), no psapi.dll

#include <cstring>

namespace uniforge::cli::platform {

namespace {

std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int n =
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

// Quotes one argument so CommandLineToArgvW / the CRT parse it back unchanged.
void appendQuoted(std::wstring& cmd, const std::wstring& arg) {
    cmd += L'"';
    std::size_t backslashes = 0;
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            cmd.append(backslashes * 2 + 1, L'\\');
        } else {
            cmd.append(backslashes, L'\\');
        }
        backslashes = 0;
        cmd += c;
    }
    cmd.append(backslashes * 2, L'\\');
    cmd += L'"';
}

} // namespace

MemoryUsage processMemory() {
    MemoryUsage m;
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                             sizeof pmc)) {
        m.workingSetBytes = pmc.WorkingSetSize;
        m.peakWorkingSetBytes = pmc.PeakWorkingSetSize;
        m.peakPrivateBytes = pmc.PeakPagefileUsage;
        m.hasPrivate = true;
    }
    return m;
}

std::filesystem::path currentExecutable() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (n == 0) {
            return {};
        }
        if (n < buffer.size()) {
            buffer.resize(n);
            return std::filesystem::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
}

int runProcess(const std::filesystem::path& exe, const std::vector<std::string>& args) {
    std::wstring cmd;
    appendQuoted(cmd, exe.wstring());
    for (const std::string& a : args) {
        cmd += L' ';
        appendQuoted(cmd, widen(a));
    }
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si,
                        &pi)) {
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    const BOOL got = GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return got ? static_cast<int>(code) : -1;
}

std::string cpuName() {
    int info[4] = {};
    __cpuid(info, static_cast<int>(0x80000000));
    if (static_cast<unsigned>(info[0]) < 0x80000004U) {
        return "unknown CPU";
    }
    char brand[49] = {};
    for (int i = 0; i < 3; ++i) {
        __cpuid(info, static_cast<int>(0x80000002U + static_cast<unsigned>(i)));
        std::memcpy(brand + 16 * i, info, 16);
    }
    std::string name(brand);
    const auto first = name.find_first_not_of(' ');
    return first == std::string::npos ? "unknown CPU" : name.substr(first);
}

std::uint64_t totalRamBytes() {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof status;
    return GlobalMemoryStatusEx(&status) ? status.ullTotalPhys : 0;
}

std::string osName() {
    return "Windows (x64)";
}

} // namespace uniforge::cli::platform
