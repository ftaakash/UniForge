#include "cli/platform/Process.hpp"

#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fstream>
#include <string>
#include <system_error>

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
