#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace uniforge::platform {

// Launch-to-ready measurement (spec §7.1, N2). Two numbers, both ending at the first Present:
//   fromLauncher: Launch.exe start, passed in as --launch-t0=<QueryPerformanceCounter value>.
//                 QPC is system-wide, so the two processes share one clock.
//   fromProcess:  UniForge.exe process creation (GetProcessTimes), which includes loader and
//                 static-initialisation time.
struct LaunchTiming {
    std::optional<double> fromLauncherSeconds;
    std::optional<double> fromProcessSeconds;
};

// Command-line options understood by UniForge.exe.
struct LaunchOptions {
    std::optional<std::int64_t> launchT0; // --launch-t0=<QPC>
    bool forceWarp = false;               // --force-warp: skip the hardware device
    bool simulateDeviceLost = false;      // --simulate-device-lost: exercise recovery once
    std::optional<bool> forceDark;        // --theme=light|dark (default: follow Windows)
    std::vector<std::string> unknown;     // ignored arguments, logged
};

// Parses the process command line (GetCommandLineW).
[[nodiscard]] LaunchOptions parseCommandLine();

// Measures both intervals now. launchT0 values from the future or <= 0 are ignored.
[[nodiscard]] LaunchTiming measureLaunchTiming(std::optional<std::int64_t> launchT0);

} // namespace uniforge::platform
