#pragma once

#include <windows.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace uniforge::platform {

// UTF-8 <-> UTF-16 conversion for the boundary between Win32 (wide) and ImGui/logs (UTF-8).
[[nodiscard]] std::string toUtf8(std::wstring_view wide);
[[nodiscard]] std::wstring toWide(std::string_view utf8);
[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path);

// System message text for an error code, e.g. "The system cannot find the file specified."
[[nodiscard]] std::string systemErrorText(DWORD code);
// systemErrorText(GetLastError()) plus the numeric code.
[[nodiscard]] std::string lastErrorText();
// "0x887A0005" style formatting for HRESULTs.
[[nodiscard]] std::string hresultText(HRESULT hr);

} // namespace uniforge::platform
