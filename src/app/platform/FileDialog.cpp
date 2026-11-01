#include "app/platform/FileDialog.hpp"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <system_error>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace uniforge::platform {

namespace {

// The dialogs are COM objects; the UI thread joins a single-threaded apartment once.
bool ensureCom() {
    static const HRESULT hr =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
}

std::vector<COMDLG_FILTERSPEC> specs(const std::vector<FileFilter>& filters) {
    std::vector<COMDLG_FILTERSPEC> out;
    out.reserve(filters.size());
    for (const auto& f : filters) {
        out.push_back({f.name.c_str(), f.pattern.c_str()});
    }
    return out;
}

void setFolder(IFileDialog* dialog, const std::filesystem::path& dir) {
    if (dir.empty()) {
        return;
    }
    ComPtr<IShellItem> folder;
    if (SUCCEEDED(SHCreateItemFromParsingName(dir.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
        dialog->SetFolder(folder.Get());
    }
}

std::optional<std::filesystem::path> resultPath(IFileDialog* dialog) {
    ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    if (FAILED(dialog->GetResult(&item)) ||
        FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        return std::nullopt;
    }
    std::filesystem::path result(path);
    CoTaskMemFree(path);
    return result;
}

} // namespace

std::optional<std::filesystem::path> openFileDialog(HWND owner,
                                                    const std::vector<FileFilter>& filters,
                                                    const std::filesystem::path& initialDir) {
    ComPtr<IFileOpenDialog> dialog;
    if (!ensureCom() || FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                                IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }
    const auto spec = specs(filters);
    dialog->SetFileTypes(static_cast<UINT>(spec.size()), spec.data());
    dialog->SetOptions(FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
    setFolder(dialog.Get(), initialDir);
    if (FAILED(dialog->Show(owner))) {
        return std::nullopt; // cancelled
    }
    return resultPath(dialog.Get());
}

std::optional<std::filesystem::path>
saveFileDialog(HWND owner, const std::vector<FileFilter>& filters, const std::wstring& defaultName,
               const std::wstring& defaultExt, const std::filesystem::path& initialDir) {
    ComPtr<IFileSaveDialog> dialog;
    if (!ensureCom() || FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                                IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }
    const auto spec = specs(filters);
    dialog->SetFileTypes(static_cast<UINT>(spec.size()), spec.data());
    dialog->SetDefaultExtension(defaultExt.c_str());
    dialog->SetFileName(defaultName.c_str());
    dialog->SetOptions(FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
    setFolder(dialog.Get(), initialDir.empty() ? defaultExportDir() : initialDir);
    if (FAILED(dialog->Show(owner))) {
        return std::nullopt;
    }
    return resultPath(dialog.Get());
}

std::filesystem::path defaultExportDir() {
    PWSTR docs = nullptr;
    std::filesystem::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_CREATE, nullptr, &docs))) {
        dir = std::filesystem::path(docs) / L"UniForge" / L"exports";
    }
    CoTaskMemFree(docs);
    if (!dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
    }
    return dir;
}

void revealInExplorer(const std::filesystem::path& file) {
    const std::wstring args = L"/select,\"" + file.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

} // namespace uniforge::platform
