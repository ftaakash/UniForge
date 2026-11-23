#include "core/viz/Spy.hpp"

#include <algorithm>
#include <cmath>

namespace uniforge::viz {

Image renderSpy(std::int64_t n, const std::vector<std::int64_t>& outerPtr,
                const std::vector<std::int32_t>& inner, bool transpose, const SpyOptions& options) {
    const int px = std::max(16, options.pixels);
    std::vector<std::int64_t> count(static_cast<std::size_t>(px) * static_cast<std::size_t>(px), 0);
    const double cell = n > 0 ? static_cast<double>(n) / px : 1.0;
    const auto bin = [&](std::int64_t i) {
        return std::min(px - 1, static_cast<int>(static_cast<double>(i) / cell));
    };
    const auto add = [&](std::int64_t row, std::int64_t col) {
        ++count[static_cast<std::size_t>(bin(row)) * static_cast<std::size_t>(px) +
                static_cast<std::size_t>(bin(col))];
    };
    const std::int64_t outerCount = static_cast<std::int64_t>(outerPtr.size()) - 1;
    for (std::int64_t o = 0; o < outerCount; ++o) {
        for (std::int64_t q = outerPtr[static_cast<std::size_t>(o)];
             q < outerPtr[static_cast<std::size_t>(o) + 1]; ++q) {
            const std::int64_t i = inner[static_cast<std::size_t>(q)];
            const std::int64_t row = transpose ? i : o;
            const std::int64_t col = transpose ? o : i;
            add(row, col);
            if (options.mirror && row != col) {
                add(col, row);
            }
        }
    }
    const std::int64_t maxCount = *std::max_element(count.begin(), count.end());
    Image img(px, px, options.background);
    if (maxCount == 0) {
        return img;
    }
    const double denom = std::log1p(static_cast<double>(maxCount));
    for (int y = 0; y < px; ++y) {
        for (int x = 0; x < px; ++x) {
            const std::int64_t c =
                count[static_cast<std::size_t>(y) * static_cast<std::size_t>(px) +
                      static_cast<std::size_t>(x)];
            if (c == 0) {
                continue;
            }
            // Any entry is visible (at least 25 % of the colour), the densest block is full.
            const double f = 0.25 + 0.75 * std::log1p(static_cast<double>(c)) / denom;
            Rgb out;
            for (std::size_t k = 0; k < 3; ++k) {
                out[k] = static_cast<std::uint8_t>(std::lround(
                    options.background[k] + f * (options.color[k] - options.background[k])));
            }
            img.set(x, y, out);
        }
    }
    return img;
}

} // namespace uniforge::viz
