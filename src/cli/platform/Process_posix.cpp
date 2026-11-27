#include <chrono>
#include <csignal>
#include <fstream>
#include <spawn.h>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <vector>

#include "cli/platform/Process.hpp"

extern char** environ;

namespace uniforge::cli::platform {

MemoryUsage processMemory() {
    MemoryUsage m;
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        m.peakWorkingSetBytes = static_cast<std::uint64_t>(usage.ru_maxrss) * 1024U; // KiB on Linux
    }
    std::ifstream statm("/proc/self/statm");
    std::uint64_t pages = 0;
    std::uint64_t resident = 0;
    if (statm >> pages >> resident) {
        m.workingSetBytes = resident * static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
    }
    return m;
}

std::filesystem::path currentExecutable() {
    std::error_code ec;
    return std::filesystem::read_symlink("/proc/self/exe", ec);
}

int runProcess(const std::filesystem::path& exe, const std::vector<std::string>& args) {
    std::string program = exe.string();
    std::vector<std::string> storage{program};
    storage.insert(storage.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& s : storage) {
        argv.push_back(s.data());
    }
    argv.push_back(nullptr);
    pid_t pid = 0;
    if (posix_spawn(&pid, program.c_str(), nullptr, nullptr, argv.data(), environ) != 0) {
        return -1;
    }
    int status = 0;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) {
        return -1;
    }
    return WEXITSTATUS(status);
}

LimitedRun runProcessLimited(const std::filesystem::path& exe, const std::vector<std::string>& args,
                             std::uint64_t /*memoryLimitBytes*/, double timeoutSeconds) {
    LimitedRun out;
    std::string program = exe.string();
    std::vector<std::string> storage{program};
    storage.insert(storage.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& s : storage) {
        argv.push_back(s.data());
    }
    argv.push_back(nullptr);
    pid_t pid = 0;
    if (posix_spawn(&pid, program.c_str(), nullptr, nullptr, argv.data(), environ) != 0) {
        return out;
    }
    out.started = true;
    const auto t0 = std::chrono::steady_clock::now();
    int status = 0;
    for (;;) {
        const pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            break;
        }
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (elapsed > timeoutSeconds) {
            out.timedOut = true;
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    out.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return out;
}

int pinToPerformanceCore() {
    return -1;
}

std::vector<int> performanceFirstCpus(int /*threads*/) {
    return {};
}

bool pinThreadToCpu(int /*cpu*/) {
    return false;
}

void keepAwake(bool /*on*/) {}

double cpuLoadPercent(double /*sampleSeconds*/) {
    return -1.0;
}

std::string cpuName() {
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("model name", 0) == 0) {
            const auto colon = line.find(':');
            if (colon != std::string::npos && colon + 2 <= line.size()) {
                return line.substr(colon + 2);
            }
        }
    }
    return "unknown CPU";
}

std::uint64_t totalRamBytes() {
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long size = sysconf(_SC_PAGESIZE);
    return pages > 0 && size > 0
               ? static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(size)
               : 0;
}

std::string osName() {
    return "Linux";
}

} // namespace uniforge::cli::platform
