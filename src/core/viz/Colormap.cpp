#include "core/viz/Colormap.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace uniforge::viz {

Rgb inferno(double t) {
    // Polynomial fit of matplotlib's inferno map by Matt Zucker (CC0, shadertoy "WlfXRN"), the
    // same fit as v1's Thermal view (app/ui/ThemeViz.cpp).
    t = std::clamp(t, 0.0, 1.0);
    constexpr double c[7][3] = {
        {0.0002189403691192265, 0.001651004631001012, -0.01948089843709184},
        {0.1065134194856116, 0.5639564367884091, 3.932712388889277},
        {11.60249308247187, -3.972853965665698, -15.9423941062914},
        {-41.70399613139459, 17.43639888205313, 44.35414519872813},
        {77.162935699427, -33.40235894210092, -81.80730925738993},
        {-71.31942824499214, 32.62606426397723, 73.20951985803202},
        {25.13112622477341, -12.24266895238567, -23.07032500287172},
    };
    Rgb rgb{};
    for (std::size_t ch = 0; ch < 3; ++ch) {
        double v = c[6][ch];
        for (int k = 5; k >= 0; --k) {
            v = c[k][ch] + t * v;
        }
        rgb[ch] = static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
    }
    return rgb;
}

Rgb inferno(double value, double lo, double hi) {
    return inferno(hi > lo ? (value - lo) / (hi - lo) : 0.5);
}

Rgb categorical(std::size_t index) {
    // Okabe–Ito hues plus lighter and darker variants, ordered so neighbours contrast.
    static constexpr std::array<Rgb, 18> palette = {{
        {0xE6, 0x9F, 0x00},
        {0x56, 0xB4, 0xE9},
        {0x00, 0x9E, 0x73},
        {0xF0, 0xE4, 0x42},
        {0x00, 0x72, 0xB2},
        {0xD5, 0x5E, 0x00},
        {0xCC, 0x79, 0xA7},
        {0x99, 0x99, 0x99},
        {0xF5, 0xC7, 0x66},
        {0x9A, 0xD2, 0xF2},
        {0x66, 0xC5, 0xAB},
        {0x80, 0x52, 0x00},
        {0x66, 0xAA, 0xD1},
        {0xE6, 0x9E, 0x66},
        {0xE0, 0xAF, 0xCA},
        {0x00, 0x4C, 0x77},
        {0x00, 0x63, 0x48},
        {0x8C, 0x3E, 0x00},
    }};
    return palette[index % palette.size()];
}

} // namespace uniforge::viz
