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

// V2 experiment harness (build spec §9):
struct LimitedRun {
    bool started = false;
    bool timedOut = false; // killed after the timeout
    int exitCode = -1;
};
// Starts `exe` with `args`, waits at most `timeoutSeconds`, and kills it on timeout. On Windows the
// child runs in a Job Object whose committed memory is capped at `memoryLimitBytes` (0 = no
// limit); a child over the cap fails its allocation and exits with an error. Elsewhere only the
// timeout applies.
[[nodiscard]] LimitedRun runProcessLimited(const std::filesystem::path& exe,
                                           const std::vector<std::string>& args,
                                           std::uint64_t memoryLimitBytes, double timeoutSeconds);
// Pins the calling thread to one logical CPU of a performance core (Intel hybrid: the highest
// efficiency class; the last such CPU, since CPU 0 usually takes most interrupts). Returns
// that CPU's index, or -1 where pinning is not supported.
[[nodiscard]] int pinToPerformanceCore();
// Logical CPUs for a pool of `threads` threads (V3e): thread 0 gets the CPU pinToPerformanceCore
// would pick, then one logical CPU on each other performance core, then the remaining
// performance-core siblings (SMT), then efficiency cores. Empty where unsupported or when
// `threads` exceeds the logical CPUs.
[[nodiscard]] std::vector<int> performanceFirstCpus(int threads);
// Pins the calling thread to one logical CPU. False where unsupported or on failure.
bool pinThreadToCpu(int cpu);
// Keeps the machine from sleeping while `on` (Windows: SetThreadExecutionState); no-op elsewhere.
void keepAwake(bool on);
// Whole-machine CPU load in percent over `sampleSeconds`, or -1 if unknown.
[[nodiscard]] double cpuLoadPercent(double sampleSeconds);

// For the benchmark report header.
[[nodiscard]] std::string cpuName();
[[nodiscard]] std::uint64_t totalRamBytes();
[[nodiscard]] std::string osName();

} // namespace uniforge::cli::platform
