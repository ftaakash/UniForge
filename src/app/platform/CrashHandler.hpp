#pragma once

#include <filesystem>

namespace uniforge::platform {

// Last-resort crash reporting (spec §13, Phase 8). On an unhandled SEH exception or
// std::terminate, writes logs\crash_<pid>.txt (exception code, address, version) and shows a
// message box that names the file. No minidump: that would add a dbghelp.dll import.
void installCrashHandler(const std::filesystem::path& logsDir);

// --simulate-crash: raises an unhandled SEH exception to exercise the handler.
[[noreturn]] void simulateCrash();

} // namespace uniforge::platform
