#pragma once

#include <cstdint>
#include <vector>

#include "core/viz/Image.hpp"

namespace uniforge::viz {

// Sparsity pattern as a density raster (build spec §12.1 P8): a matrix with millions of
// entries cannot be drawn dot by dot, so each pixel counts the entries of its block and is
// shaded on a log scale from the background (empty) to the colour (densest block).
struct SpyOptions {
    int pixels = 600;            // image is pixels × pixels
    Rgb color{0x00, 0x72, 0xB2}; // direct-solver blue (figure tokens)
    Rgb background{255, 255, 255};
    bool mirror = false; // also draw (j, i) for every (i, j): a triangular factor as L + Lᵀ
};

// The pattern in compressed form: entries of outer index o are inner[outerPtr[o] …
// outerPtr[o+1]). For CSR, outer = row and inner = column; for a column-stored factor, outer =
// column and inner = row (pass transpose = true to draw it with rows down).
[[nodiscard]] Image renderSpy(std::int64_t n, const std::vector<std::int64_t>& outerPtr,
                              const std::vector<std::int32_t>& inner, bool transpose,
                              const SpyOptions& options = {});

} // namespace uniforge::viz
