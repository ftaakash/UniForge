#include "app/platform/Paths.hpp"

#include <windows.h>

#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <system_error>

namespace uniforge::platform {

namespace {

std::filesystem::path executablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD len =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0) {
            return {};
        }
        if (len < buffer.size()) {
            buffer.resize(len);
            return buffer;
        }
        buffer.resize(buffer.size() * 2); // long path: retry with a bigger buffer
    }
}

// Creates and immediately deletes a temporary file: the only reliable writability test
// (folder attributes and ACL inspection miss read-only media and redirected folders).
bool canWriteTo(const std::filesystem::path& dir) {
    const std::filesystem::path probe = dir / L".uniforge_write_probe.tmp";
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    CloseHandle(h);
    return true;
}

std::filesystem::path localAppData() {
    PWSTR raw = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
        result = raw;
    }
    CoTaskMemFree(raw);
    return result;
}

bool equalsIgnoreCase(const std::wstring& a, const wchar_t* b) {
    return CompareStringOrdinal(a.c_str(), -1, b, -1, TRUE) == CSTR_EQUAL;
}

} // namespace

AppPaths resolveAppPaths() {
    AppPaths p;
    p.exeDir = executablePath().parent_path();
    // Packaged layout: <package>\app\UniForge.exe. Dev builds run straight from the build tree.
    p.packageDir =
        equalsIgnoreCase(p.exeDir.filename().native(), L"app") ? p.exeDir.parent_path() : p.exeDir;
    p.packageWritable = canWriteTo(p.packageDir);
    p.stateDir = p.packageWritable ? p.packageDir : localAppData() / L"UniForge";
    p.logsDir = p.stateDir / L"logs";
    p.iniFile = p.stateDir / L"imgui.ini";

    std::error_code ec;
    std::filesystem::create_directories(p.logsDir, ec);
    return p;
}

void openInExplorer(const std::filesystem::path& folder) {
    ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace uniforge::platform
