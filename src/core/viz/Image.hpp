#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace uniforge::viz {

using Rgb = std::array<std::uint8_t, 3>;

// "#RRGGBB" → colour (black for malformed text). Used for the figure tokens.
[[nodiscard]] Rgb hexColor(const std::string& hex);
[[nodiscard]] std::string toHex(const Rgb& c);

// 8-bit RGBA raster, rows top to bottom (the layout PngWriter expects).
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;

    Image() = default;
    Image(int w, int h, const Rgb& background);

    void set(int x, int y, const Rgb& c);
    [[nodiscard]] Rgb get(int x, int y) const;
    void fillRect(int x0, int y0, int x1, int y1, const Rgb& c); // half-open [x0, x1) × [y0, y1)

    // Box-filter downsampling by an integer factor (supersampling antialias).
    [[nodiscard]] Image downsample(int factor) const;
};

} // namespace uniforge::viz
