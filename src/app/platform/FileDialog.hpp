#pragma once

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace uniforge::platform {

struct FileFilter {
    std::wstring name;    // L"ASCII STL (*.stl)"
    std::wstring pattern; // L"*.stl"
};

// Native Windows Open / Save dialogs (IFileOpenDialog / IFileSaveDialog). Modal on the UI
// thread; nullopt when the user cancels. Paths are wide, so spaces and Unicode survive.
[[nodiscard]] std::optional<std::filesystem::path>
openFileDialog(HWND owner, const std::vector<FileFilter>& filters,
               const std::filesystem::path& initialDir = {});

// `defaultName` e.g. L"convergence.csv"; `defaultExt` without the dot, e.g. L"csv".
[[nodiscard]] std::optional<std::filesystem::path>
saveFileDialog(HWND owner, const std::vector<FileFilter>& filters, const std::wstring& defaultName,
               const std::wstring& defaultExt, const std::filesystem::path& initialDir = {});

// Documents\UniForge\exports (created on demand): the default export folder (spec §7.1).
[[nodiscard]] std::filesystem::path defaultExportDir();

// Opens Explorer with `file` selected (the "Open folder" action after an export).
void revealInExplorer(const std::filesystem::path& file);

} // namespace uniforge::platform
