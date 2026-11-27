#include <windows.h>

#include <algorithm>
#include <cstring>
#include <intrin.h>
#include <psapi.h> // K32GetProcessMemoryInfo lives in kernel32 (PSAPI_VERSION 2), no psapi.dll
#include <vector>

#include "cli/platform/Process.hpp"

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

LimitedRun runProcessLimited(const std::filesystem::path& exe, const std::vector<std::string>& args,
                             std::uint64_t memoryLimitBytes, double timeoutSeconds) {
    LimitedRun out;
    std::wstring cmd;
    appendQuoted(cmd, exe.wstring());
    for (const std::string& a : args) {
        cmd += L' ';
        appendQuoted(cmd, widen(a));
    }
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) {
        return out;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (memoryLimitBytes > 0) {
        limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        limits.ProcessMemoryLimit = static_cast<SIZE_T>(memoryLimitBytes);
    }
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
                        nullptr, &si, &pi)) {
        CloseHandle(job);
        return out;
    }
    out.started = true;
    AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    const double ms = timeoutSeconds * 1000.0;
    const DWORD wait = ms >= 4.0e9 ? INFINITE : static_cast<DWORD>(ms);
    if (WaitForSingleObject(pi.hProcess, wait) == WAIT_TIMEOUT) {
        out.timedOut = true;
        TerminateJobObject(job, 1);
        WaitForSingleObject(pi.hProcess, INFINITE);
    }
    DWORD code = 0;
    if (GetExitCodeProcess(pi.hProcess, &code)) {
        out.exitCode = static_cast<int>(code);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(job);
    return out;
}

int pinToPerformanceCore() {
    ULONG length = 0;
    GetSystemCpuSetInformation(nullptr, 0, &length, GetCurrentProcess(), 0);
    if (length == 0) {
        return -1;
    }
    std::vector<unsigned char> buffer(length);
    auto* info = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data());
    if (!GetSystemCpuSetInformation(info, length, &length, GetCurrentProcess(), 0)) {
        return -1;
    }
    int best = -1;
    int bestClass = -1;
    for (ULONG offset = 0; offset < length;) {
        const auto* entry =
            reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buffer.data() + offset);
        if (entry->Type == CpuSetInformation && entry->CpuSet.Group == 0 &&
            entry->CpuSet.LogicalProcessorIndex < 64 &&
            static_cast<int>(entry->CpuSet.EfficiencyClass) >= bestClass) {
            bestClass = static_cast<int>(entry->CpuSet.EfficiencyClass);
            best = static_cast<int>(entry->CpuSet.LogicalProcessorIndex);
        }
        offset += entry->Size;
    }
    if (best < 0 || SetThreadAffinityMask(GetCurrentThread(),
                                          DWORD_PTR{1} << static_cast<unsigned>(best)) == 0) {
        return -1;
    }
    return best;
}

std::vector<int> performanceFirstCpus(int threads) {
    ULONG length = 0;
    GetSystemCpuSetInformation(nullptr, 0, &length, GetCurrentProcess(), 0);
    if (length == 0 || threads < 1) {
        return {};
    }
    std::vector<unsigned char> buffer(length);
    auto* info = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data());
    if (!GetSystemCpuSetInformation(info, length, &length, GetCurrentProcess(), 0)) {
        return {};
    }
    struct Cpu {
        int index;
        int efficiencyClass;
        int core;
    };
    std::vector<Cpu> cpus;
    for (ULONG offset = 0; offset < length;) {
        const auto* entry =
            reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buffer.data() + offset);
        if (entry->Type == CpuSetInformation && entry->CpuSet.Group == 0 &&
            entry->CpuSet.LogicalProcessorIndex < 64) {
            cpus.push_back({static_cast<int>(entry->CpuSet.LogicalProcessorIndex),
                            static_cast<int>(entry->CpuSet.EfficiencyClass),
                            static_cast<int>(entry->CpuSet.CoreIndex)});
        }
        offset += entry->Size;
    }
    if (cpus.empty() || static_cast<std::size_t>(threads) > cpus.size()) {
        return {};
    }
    int topClass = -1;
    for (const Cpu& c : cpus) {
        topClass = std::max(topClass, c.efficiencyClass);
    }
    // Thread 0: the same CPU as the one-thread runs (the last performance-class CPU).
    int first = -1;
    for (const Cpu& c : cpus) {
        if (c.efficiencyClass == topClass) {
            first = std::max(first, c.index);
        }
    }
    std::vector<int> order{first};
    std::vector<char> used(64, 0);
    std::vector<char> coreUsed(256, 0);
    used[static_cast<std::size_t>(first)] = 1;
    for (const Cpu& c : cpus) {
        if (c.index == first) {
            coreUsed[static_cast<std::size_t>(c.core & 255)] = 1;
        }
    }
    const auto take = [&](bool performance, bool newCoreOnly) {
        for (const Cpu& c : cpus) { // ascending logical index
            const bool isPerformance = c.efficiencyClass == topClass;
            if (used[static_cast<std::size_t>(c.index)] || isPerformance != performance) {
                continue;
            }
            if (newCoreOnly && coreUsed[static_cast<std::size_t>(c.core & 255)]) {
                continue;
            }
            order.push_back(c.index);
            used[static_cast<std::size_t>(c.index)] = 1;
            coreUsed[static_cast<std::size_t>(c.core & 255)] = 1;
        }
    };
    take(true, true);   // one per remaining performance core
    take(true, false);  // their SMT siblings
    take(false, false); // efficiency cores
    order.resize(static_cast<std::size_t>(threads));
    return order;
}

bool pinThreadToCpu(int cpu) {
    if (cpu < 0 || cpu >= 64) {
        return false;
    }
    return SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR{1} << static_cast<unsigned>(cpu)) !=
           0;
}

void keepAwake(bool on) {
    SetThreadExecutionState(on ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED) : ES_CONTINUOUS);
}

double cpuLoadPercent(double sampleSeconds) {
    const auto ticks = [](const FILETIME& f) {
        return (static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    };
    FILETIME idle1{}, kernel1{}, user1{}, idle2{}, kernel2{}, user2{};
    if (!GetSystemTimes(&idle1, &kernel1, &user1)) {
        return -1.0;
    }
    Sleep(static_cast<DWORD>(sampleSeconds * 1000.0));
    if (!GetSystemTimes(&idle2, &kernel2, &user2)) {
        return -1.0;
    }
    const double idle = static_cast<double>(ticks(idle2) - ticks(idle1));
    const double total =
        static_cast<double>((ticks(kernel2) - ticks(kernel1)) + (ticks(user2) - ticks(user1)));
    return total > 0.0 ? 100.0 * (1.0 - idle / total) : -1.0;
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
