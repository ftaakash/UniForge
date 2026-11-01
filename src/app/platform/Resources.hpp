#pragma once

#include <cstddef>

namespace uniforge::platform {

// Read-only view of an RCDATA resource embedded in UniForge.exe (see app.rc.in).
// The memory belongs to the loaded module and stays valid for the process lifetime.
struct ResourceBytes {
    const void* data = nullptr;
    std::size_t size = 0;
};

// Returns an empty view if the resource is missing.
[[nodiscard]] ResourceBytes loadResource(int id);

} // namespace uniforge::platform
