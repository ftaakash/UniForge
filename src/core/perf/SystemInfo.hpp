#pragma once

#include <cstdint>
#include <string>

namespace uniforge::perf {

// Machine and build facts for the benchmark report (spec §5.3, D3). The only OS-specific code
// in uniforge_core sits behind querySystemInfo(): SystemInfo_win.cpp and SystemInfo_linux.cpp.
struct SystemInfo {
    std::string cpu;       // brand string, e.g. "Intel(R) Core(TM) i5-14600K"
    std::string coreTypes; // e.g. "6 performance + 8 efficiency cores (hybrid)", or empty
    std::string os;        // e.g. "Windows 11 (10.0.26200), x64"
    std::string compiler;  // e.g. "MSVC 19.44.35217"
    std::string flags;     // optimisation and code-generation flags of this build
    std::string buildType; // "Release" or "Debug (not optimised: ...)"
    int physicalCores = 0;
    int logicalCpus = 0;
    std::uint64_t ramBytes = 0;
};

// Queries the running machine. Fields that cannot be determined stay empty / 0 ("unknown" in
// reports); logicalCpus falls back to std::thread::hardware_concurrency() and is at least 1.
[[nodiscard]] SystemInfo querySystemInfo();

// Compiler name + version, build flags and build type, from predefined macros and the build
// defines. Shared by both platform files.
[[nodiscard]] std::string compilerDescription();
[[nodiscard]] std::string buildFlags();
[[nodiscard]] std::string buildTypeDescription();

} // namespace uniforge::perf
