#pragma once

#include <filesystem>
#include <string_view>

#include "core/common/Result.hpp"

namespace uniforge {

// Writes `content` byte for byte (binary mode, so LF stays LF on Windows) to `<path>.tmp`, then
// renames it over `path` (spec §6 "atomic export"). A reader never sees a half-written file, and
// on any failure the previous file at `path` is left untouched. Missing parent directories are
// created. Errors are IoError with the path in the message.
Result<void> writeFileAtomic(const std::filesystem::path& path, std::string_view content);

} // namespace uniforge
