#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/common/Result.hpp"

namespace uniforge {

// Colours for evidence plots, matching the UI tokens (spec §8.2). Series use the Okabe–Ito
// palette, which stays distinguishable for the common forms of colour-vision deficiency.
namespace plotcolor {
inline constexpr const char* kSeries[] = {"#D55E00", "#0072B2", "#009E73", "#CC79A7",
                                          "#E69F00", "#56B4E9"};
inline constexpr const char* kReference = "#1A1E24"; // text.primary (light)
inline constexpr const char* kTextSecondary = "#59616E";
inline constexpr const char* kGrid = "#D3D9E1"; // border.default (light)
inline constexpr const char* kBackground = "#FFFFFF";
} // namespace plotcolor

enum class PlotMarker { None, Circle, Square, Triangle, Diamond };

struct PlotSeries {
    std::string label; // shown in the legend
    std::vector<double> x, y;
    std::string color = plotcolor::kSeries[0];
    bool line = true;    // connect the points
    bool dashed = false; // reference style: dashed, drawn on top when listed last
    PlotMarker marker = PlotMarker::None;
    double strokeWidth = 1.5; // px
};

struct PlotSpec {
    std::string title;
    std::string xLabel; // include the unit, e.g. "x (m)"
    std::string yLabel;
    bool logX = false;
    bool logY = false;
    int width = 820; // px
    int height = 500;
    std::vector<PlotSeries> series; // drawn in order, so later series sit on top
};

// Renders a small, deterministic SVG line plot: same spec → same bytes (coordinates are written
// with two decimals, no timestamps or random ids). Linear axes get 1-2-5 "nice" ticks; log axes
// get ticks at 1, 2 and 5 times each power of ten. The legend sits to the right of the plot area
// so it never hides data.
// Errors (InvalidInput): no series, mismatched or empty x/y, non-finite values, or values ≤ 0
// on a log axis.
[[nodiscard]] Result<std::string> renderSvgPlot(const PlotSpec& spec);

// renderSvgPlot + writeFileAtomic.
Result<void> saveSvgPlot(const PlotSpec& spec, const std::filesystem::path& path);

} // namespace uniforge
