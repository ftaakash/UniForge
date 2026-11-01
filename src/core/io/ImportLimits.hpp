#pragma once

#include <cstddef>
#include <cstdint>

namespace uniforge::io {

// Upper bounds the importers enforce before they run out of memory or time (spec §7.2, B3).
// The defaults are the largest sizes measured by `uniforge_cli import-bench`
// (results/benchmarks/import_limits.md); a bigger file is rejected with LimitExceeded and the
// limit in the hint, instead of being half-loaded.
struct ImportLimits {
    std::uintmax_t maxBytes = 512ULL << 20; // file size, bytes
    std::size_t maxFacets = 2'000'000;      // STL triangles
    std::size_t maxNodes = 5'000'000;       // CSV data rows
    std::size_t maxLineBytes = 4096;        // one text line, line end excluded
};

// Size of the LineReader's read buffer. A line never needs more than this, so maxLineBytes is
// clamped to it.
inline constexpr std::size_t kLineReaderBufferBytes = 64 * 1024;

} // namespace uniforge::io
