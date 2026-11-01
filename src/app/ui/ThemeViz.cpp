#include "app/ui/ThemeViz.hpp"

#include <cstdint>

#include "app/ui/Theme.hpp"

namespace uniforge::ui {

namespace {

constexpr ImVec4 hex(std::uint32_t rgb) {
    return ImVec4(static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
                  static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                  static_cast<float>(rgb & 0xFF) / 255.0f, 1.0f);
}

constexpr ImVec4 kSeries[] = {hex(0xD55E00), hex(0x0072B2), hex(0x009E73),
                              hex(0xCC79A7), hex(0xE69F00), hex(0x56B4E9)};
constexpr int kSeriesCount = static_cast<int>(sizeof kSeries / sizeof kSeries[0]);

bool dark() {
    return theme().mode == ThemeMode::Dark;
}

} // namespace

ImVec4 seriesColor(int index) {
    const int i = ((index % kSeriesCount) + kSeriesCount) % kSeriesCount;
    return kSeries[i];
}

int seriesIndexForMesh(int elements) {
    switch (elements) {
    case 10:
        return 0;
    case 20:
        return 1;
    case 40:
        return 2;
    case 80:
        return 3;
    default:
        return 4 + (elements & 1);
    }
}

ImVec4 referenceColor() {
    return theme().color.textPrimary;
}

ImVec4 meshFillColor() {
    return dark() ? hex(0x5D6B7A) : hex(0x9FB3C8);
}

ImVec4 meshWireColor() {
    return dark() ? hex(0x9AA3B2) : hex(0x3A4756);
}

ImVec4 pickColor() {
    return hex(0x00B3FF);
}

ImVec4 viewportBackground() {
    return theme().color.bgSurfaceAlt;
}

ImVec4 axisColor(int axis) {
    // Red/green/blue are the CAD convention for x/y/z; the axis letters are drawn as well, so
    // the triad does not rely on colour alone.
    constexpr ImVec4 kLight[] = {hex(0xC0392B), hex(0x1E8449), hex(0x2563EB)};
    constexpr ImVec4 kDark[] = {hex(0xF06A5D), hex(0x4CC27A), hex(0x5B95FF)};
    const int i = axis < 0 ? 0 : (axis > 2 ? 2 : axis);
    return dark() ? kDark[i] : kLight[i];
}

std::array<float, 3> inferno(float t) {
    // Polynomial fit of matplotlib's inferno map by Matt Zucker (CC0, shadertoy "WlfXRN").
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    constexpr float c[7][3] = {
        {0.0002189403691192265f, 0.001651004631001012f, -0.01948089843709184f},
        {0.1065134194856116f, 0.5639564367884091f, 3.932712388889277f},
        {11.60249308247187f, -3.972853965665698f, -15.9423941062914f},
        {-41.70399613139459f, 17.43639888205313f, 44.35414519872813f},
        {77.162935699427f, -33.40235894210092f, -81.80730925738993f},
        {-71.31942824499214f, 32.62606426397723f, 73.20951985803202f},
        {25.13112622477341f, -12.24266895238567f, -23.07032500287172f},
    };
    std::array<float, 3> rgb{};
    for (int ch = 0; ch < 3; ++ch) {
        float v = c[6][ch];
        for (int k = 5; k >= 0; --k) {
            v = c[k][ch] + t * v;
        }
        rgb[static_cast<std::size_t>(ch)] = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }
    return rgb;
}

ImVec4 colormapColor(float t) {
    const auto rgb = inferno(t);
    return ImVec4(rgb[0], rgb[1], rgb[2], 1.0f);
}

} // namespace uniforge::ui
