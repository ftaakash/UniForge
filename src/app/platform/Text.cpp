#include "app/platform/Text.hpp"

#include <cstdio>

namespace uniforge::platform {

std::string toUtf8(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int wideLen = static_cast<int>(wide.size());
    const int len =
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLen, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLen, out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring toWide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int utf8Len = static_cast<int>(utf8.size());
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), utf8Len, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), utf8Len, out.data(), len);
    return out;
}

std::string pathToUtf8(const std::filesystem::path& path) {
    return toUtf8(path.native());
}

std::string systemErrorText(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text = len ? std::wstring(buffer, len) : L"Unknown error";
    LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) {
        text.pop_back();
    }
    return toUtf8(text);
}

std::string lastErrorText() {
    const DWORD code = GetLastError();
    return systemErrorText(code) + " (error " + std::to_string(code) + ")";
}

std::string hresultText(HRESULT hr) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

} // namespace uniforge::platform
