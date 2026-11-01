#include "app/platform/Resources.hpp"

#include <windows.h>

namespace uniforge::platform {

ResourceBytes loadResource(int id) {
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC info = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10)); // RT_RCDATA
    if (!info) {
        return {};
    }
    HGLOBAL handle = LoadResource(module, info);
    const void* data = handle ? LockResource(handle) : nullptr;
    if (!data) {
        return {};
    }
    return {data, SizeofResource(module, info)};
}

} // namespace uniforge::platform
