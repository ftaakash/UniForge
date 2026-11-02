#include "app/platform/LaunchTiming.hpp"

#include <windows.h>

#include <charconv>
#include <shellapi.h>

#include "app/platform/Text.hpp"

namespace uniforge::platform {

namespace {

std::uint64_t toTicks(const FILETIME& ft) {
    return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

bool startsWith(const std::string& s, const char* prefix, std::string& rest) {
    const std::string p(prefix);
    if (s.compare(0, p.size(), p) != 0) {
        return false;
    }
    rest = s.substr(p.size());
    return true;
}

} // namespace

LaunchOptions parseCommandLine() {
    LaunchOptions options;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        const std::string arg = toUtf8(argv[i]);
        std::string value;
        if (startsWith(arg, "--launch-t0=", value)) {
            std::int64_t t0 = 0;
            const auto res = std::from_chars(value.data(), value.data() + value.size(), t0);
            if (res.ec == std::errc{} && res.ptr == value.data() + value.size()) {
                options.launchT0 = t0;
            } else {
                options.unknown.push_back(arg);
            }
        } else if (arg == "--force-warp") {
            options.forceWarp = true;
        } else if (arg == "--simulate-device-lost") {
            options.simulateDeviceLost = true;
        } else if (arg == "--simulate-crash") {
            options.simulateCrash = true;
        } else if (startsWith(arg, "--theme=", value) && (value == "light" || value == "dark")) {
            options.forceDark = (value == "dark");
        } else {
            options.unknown.push_back(arg);
        }
    }
    LocalFree(argv);
    return options;
}

LaunchTiming measureLaunchTiming(std::optional<std::int64_t> launchT0) {
    LaunchTiming timing;

    LARGE_INTEGER now{};
    LARGE_INTEGER freq{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    if (launchT0 && *launchT0 > 0 && *launchT0 <= now.QuadPart) {
        timing.fromLauncherSeconds =
            static_cast<double>(now.QuadPart - *launchT0) / static_cast<double>(freq.QuadPart);
    }

    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    FILETIME nowFt{};
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        GetSystemTimePreciseAsFileTime(&nowFt);
        const std::uint64_t start = toTicks(created);
        const std::uint64_t end = toTicks(nowFt);
        if (end >= start) {
            timing.fromProcessSeconds = static_cast<double>(end - start) * 1e-7; // 100 ns ticks
        }
    }
    return timing;
}

} // namespace uniforge::platform
