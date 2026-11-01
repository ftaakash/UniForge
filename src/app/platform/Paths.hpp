#pragma once

#include <filesystem>

namespace uniforge::platform {

// Where UniForge reads and writes, resolved once at start-up (spec §7.1, §9).
struct AppPaths {
    std::filesystem::path exeDir;     // folder of UniForge.exe
    std::filesystem::path packageDir; // package root: parent of app\, or exeDir for dev builds
    std::filesystem::path stateDir;   // packageDir if writable, else %LOCALAPPDATA%\UniForge
    std::filesystem::path logsDir;    // stateDir\logs
    std::filesystem::path iniFile;    // stateDir\imgui.ini
    bool packageWritable = false;     // result of the start-up write probe
};

// Resolves the paths from the executable location (never the working directory) and runs a
// write probe on the package folder. Falls back to %LOCALAPPDATA%\UniForge when the package
// is read-only (e.g. extracted to a protected folder or a read-only share).
[[nodiscard]] AppPaths resolveAppPaths();

// Opens a folder in Explorer.
void openInExplorer(const std::filesystem::path& folder);

} // namespace uniforge::platform
