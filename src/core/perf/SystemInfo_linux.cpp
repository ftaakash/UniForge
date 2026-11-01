// Linux counterpart of SystemInfo_win.cpp: /proc/cpuinfo, /proc/meminfo, /etc/os-release.
#include "core/perf/SystemInfo.hpp"

#include <sys/utsname.h>
#include <unistd.h>

#include <fstream>
#include <set>
#include <string>
#include <thread>
#include <utility>

namespace uniforge::perf {

namespace {

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\"");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(" \t\"");
    return s.substr(first, last - first + 1);
}

// The value of a "key : value" line, or empty when `line` does not start with `key`.
std::string valueOf(const std::string& line, const std::string& key) {
    if (line.compare(0, key.size(), key) != 0) {
        return {};
    }
    const auto colon = line.find(':');
    return colon == std::string::npos ? std::string() : trim(line.substr(colon + 1));
}

void readCpuInfo(SystemInfo& info) {
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    std::string physicalId;
    std::set<std::pair<std::string, std::string>> cores; // (physical id, core id)
    while (std::getline(in, line)) {
        if (info.cpu.empty()) {
            info.cpu = valueOf(line, "model name");
        }
        if (auto v = valueOf(line, "physical id"); !v.empty()) {
            physicalId = v;
        }
        if (auto v = valueOf(line, "core id"); !v.empty()) {
            cores.emplace(physicalId, v);
        }
    }
    info.physicalCores = static_cast<int>(cores.size());
}

std::string osName() {
    std::ifstream in("/etc/os-release");
    std::string line;
    std::string name;
    while (std::getline(in, line)) {
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            name = trim(line.substr(12));
        }
    }
    utsname u{};
    if (uname(&u) == 0) {
        name += (name.empty() ? "" : ", ") + std::string(u.sysname) + " " + u.release + " " +
                u.machine;
    }
    return name.empty() ? std::string("Linux") : name;
}

} // namespace

SystemInfo querySystemInfo() {
    SystemInfo info;
    readCpuInfo(info);
    const long online = sysconf(_SC_NPROCESSORS_ONLN);
    info.logicalCpus = online > 0 ? static_cast<int>(online)
                                  : static_cast<int>(std::thread::hardware_concurrency());
    if (info.logicalCpus < 1) {
        info.logicalCpus = 1;
    }
    std::ifstream mem("/proc/meminfo");
    std::string line;
    while (std::getline(mem, line)) {
        if (auto v = valueOf(line, "MemTotal"); !v.empty()) {
            info.ramBytes = static_cast<std::uint64_t>(std::stoull(v)) * 1024U; // "16318164 kB"
            break;
        }
    }
    info.os = osName();
    info.compiler = compilerDescription();
    info.flags = buildFlags();
    info.buildType = buildTypeDescription();
    return info;
}

} // namespace uniforge::perf
