#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// The few operating-system services `uniforge_cli import-bench` needs. One header, one source
// file per platform (Process_win.cpp, Process_posix.cpp), so the commands and src/core stay
// portable C++17.
namespace uniforge::cli::platform {

struct MemoryUsage {
    std::uint64_t workingSetBytes = 0;     // resident memory now
    std::uint64_t peakWorkingSetBytes = 0; // resident memory peak since process start
    std::uint64_t peakPrivateBytes = 0;    // committed private memory peak (Windows only)
    bool hasPrivate = false;
};

// Memory of the calling process: GetProcessMemoryInfo on Windows, getrusage + /proc on Linux.
[[nodiscard]] MemoryUsage processMemory();

// Full path of the running executable.
[[nodiscard]] std::filesystem::path currentExecutable();

// Starts `exe` with `args` (UTF-8), waits for it and returns its exit code, or -1 if it could not
// be started. Standard output and error are inherited.
[[nodiscard]] int runProcess(const std::filesystem::path& exe,
                             const std::vector<std::string>& args);

// For the benchmark report header.
[[nodiscard]] std::string cpuName();
[[nodiscard]] std::uint64_t totalRamBytes();
[[nodiscard]] std::string osName();

} // namespace uniforge::cli::platform
