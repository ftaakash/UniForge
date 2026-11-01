// The one Windows-specific file in uniforge_core (spec §4.2): CPU topology, memory and OS
// version for the benchmark report.
#include "core/perf/SystemInfo.hpp"

#include <intrin.h>
#include <windows.h>

#include <cstring>
#include <map>
#include <thread>
#include <vector>

namespace uniforge::perf {

namespace {

std::string cpuBrand() {
    int info[4] = {};
    __cpuid(info, static_cast<int>(0x80000000U));
    if (static_cast<unsigned>(info[0]) < 0x80000004U) {
        return {};
    }
    char brand[49] = {};
    for (int i = 0; i < 3; ++i) {
        __cpuid(info, static_cast<int>(0x80000002U + static_cast<unsigned>(i)));
        std::memcpy(brand + 16 * i, info, 16);
    }
    const std::string name(brand);
    const auto first = name.find_first_not_of(' ');
    const auto last = name.find_last_not_of(' ');
    return first == std::string::npos ? std::string() : name.substr(first, last - first + 1);
}

// Physical cores, logical CPUs and cores per efficiency class. Hybrid Intel CPUs report the
// P-cores with a higher EfficiencyClass than the E-cores.
void cpuTopology(SystemInfo& info) {
    DWORD bytes = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0) {
        return;
    }
    std::vector<unsigned char> buffer(bytes);
    auto* first = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, first, &bytes)) {
        return;
    }
    int cores = 0;
    int logical = 0;
    std::map<int, int> coresPerClass; // EfficiencyClass -> cores
    for (DWORD offset = 0; offset < bytes;) {
        const auto* entry = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
            buffer.data() + offset);
        if (entry->Relationship == RelationProcessorCore) {
            ++cores;
            ++coresPerClass[entry->Processor.EfficiencyClass];
            for (WORD g = 0; g < entry->Processor.GroupCount; ++g) {
                KAFFINITY mask = entry->Processor.GroupMask[g].Mask;
                while (mask != 0) {
                    logical += static_cast<int>(mask & 1U);
                    mask >>= 1U;
                }
            }
        }
        offset += entry->Size;
    }
    info.physicalCores = cores;
    info.logicalCpus = logical;
    if (coresPerClass.size() == 2) {
        info.coreTypes = std::to_string(coresPerClass.rbegin()->second) + " performance + " +
                         std::to_string(coresPerClass.begin()->second) +
                         " efficiency cores (hybrid)";
    }
}

std::string osVersion() {
    // GetVersionEx reports a compatibility version; RtlGetVersion reports the real build.
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        return "Windows, x64";
    }
    const auto rtlGetVersion =
        reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof v;
    if (rtlGetVersion == nullptr || rtlGetVersion(&v) != 0) {
        return "Windows, x64";
    }
    // Windows 11 still reports major version 10; build 22000 and later is Windows 11.
    const char* name = "Windows";
    if (v.dwMajorVersion == 10) {
        name = v.dwBuildNumber >= 22000 ? "Windows 11" : "Windows 10";
    }
    return std::string(name) + " (" + std::to_string(v.dwMajorVersion) + "." +
           std::to_string(v.dwMinorVersion) + "." + std::to_string(v.dwBuildNumber) + "), x64";
}

} // namespace

SystemInfo querySystemInfo() {
    SystemInfo info;
    info.cpu = cpuBrand();
    cpuTopology(info);
    if (info.logicalCpus < 1) {
        info.logicalCpus = static_cast<int>(std::thread::hardware_concurrency());
    }
    if (info.logicalCpus < 1) {
        info.logicalCpus = 1;
    }
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof status;
    if (GlobalMemoryStatusEx(&status)) {
        info.ramBytes = status.ullTotalPhys;
    }
    info.os = osVersion();
    info.compiler = compilerDescription();
    info.flags = buildFlags();
    info.buildType = buildTypeDescription();
    return info;
}

} // namespace uniforge::perf
