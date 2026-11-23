#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "core/common/Result.hpp"

namespace uniforge::viz {

// A figure page in SVG: raster renders embedded as base64 PNG, with text labels, leader lines,
// boxes and colour bars drawn as vectors on top (labels stay sharp in the PDF; build spec
// §12.1 "Raster3D + SVG overlay"). Deterministic: coordinates with two decimals, no ids or
// timestamps. Fonts follow the figure tokens (Inter for text, JetBrains Mono for numbers).
class SvgCanvas {
public:
    SvgCanvas(int width, int height, const std::string& title);

    void image(const std::vector<std::uint8_t>& png, double x, double y, double w, double h);
    void text(double x, double y, const std::string& text, double size = 13.0,
              const std::string& anchor = "start", const std::string& color = "#1A1E24",
              bool bold = false, bool mono = false);
    void line(double x1, double y1, double x2, double y2, const std::string& color = "#1A1E24",
              double width = 1.0, bool dashed = false);
    void rect(double x, double y, double w, double h, const std::string& fill,
              const std::string& stroke = "none", double strokeWidth = 1.0, double radius = 0.0);
    void circle(double cx, double cy, double r, const std::string& fill,
                const std::string& stroke = "none");
    void polygon(const std::vector<std::array<double, 2>>& points, const std::string& fill,
                 const std::string& stroke = "none");
    // Arrow from (x1, y1) to (x2, y2) with a filled head.
    void arrow(double x1, double y1, double x2, double y2, const std::string& color = "#1A1E24",
               double width = 1.4);
    // Vertical inferno colour bar with min/max and intermediate ticks (°C).
    void colorbar(double x, double y, double w, double h, double lo, double hi,
                  const std::string& label, int ticks = 5);
    // Multi-line text block: lines separated by '\n'.
    void textBlock(double x, double y, const std::string& text, double size = 12.0,
                   double lineHeight = 1.35, const std::string& color = "#1A1E24");
    void raw(const std::string& svg); // already-escaped SVG fragment

    [[nodiscard]] std::string str() const;
    Result<void> save(const std::filesystem::path& path) const;

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }

private:
    int width_;
    int height_;
    std::string title_;
    std::string body_;
};

[[nodiscard]] std::string base64(const std::vector<std::uint8_t>& data);
[[nodiscard]] std::string escapeXmlText(const std::string& text);
// Number with at most `digits` significant digits, no exponent below 1e6 (labels).
[[nodiscard]] std::string labelNumber(double v, int digits = 3);

} // namespace uniforge::viz
