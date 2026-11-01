#include "core/common/SvgPlot.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "core/common/AtomicFile.hpp"

namespace uniforge {

namespace {

constexpr double kMarginLeft = 80.0;
constexpr double kMarginRight = 230.0; // legend column
constexpr double kMarginTop = 48.0;
constexpr double kMarginBottom = 62.0;
constexpr const char* kFont = "Inter, 'Segoe UI', Arial, sans-serif";

std::string num(double v) {
    char buffer[32];
    const int n = std::snprintf(buffer, sizeof buffer, "%.2f", v);
    return std::string(buffer, n > 0 ? static_cast<std::size_t>(n) : 0U);
}

std::string tickLabel(double v) {
    char buffer[32];
    const int n = std::snprintf(buffer, sizeof buffer, "%g", v);
    return std::string(buffer, n > 0 ? static_cast<std::size_t>(n) : 0U);
}

std::string escapeXml(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        switch (ch) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out += ch;
        }
    }
    return out;
}

// One axis: data range → pixel range, with its tick values.
struct Axis {
    bool log = false;
    double lo = 0.0; // axis limits in data units
    double hi = 1.0;
    std::vector<double> ticks;

    [[nodiscard]] double t(double v) const { return log ? std::log10(v) : v; }
    // Fraction of the axis length, 0 at lo and 1 at hi.
    [[nodiscard]] double frac(double v) const { return (t(v) - t(lo)) / (t(hi) - t(lo)); }
};

// 1, 2 or 5 times a power of ten, at least `raw`.
double niceStep(double raw) {
    const double p = std::pow(10.0, std::floor(std::log10(raw)));
    for (const double m : {1.0, 2.0, 5.0, 10.0}) {
        if (m * p >= raw * (1.0 - 1e-12)) {
            return m * p;
        }
    }
    return 10.0 * p;
}

Axis linearAxis(double lo, double hi) {
    if (hi - lo <= std::abs(hi) * 1e-12) { // flat data: open a symmetric window around it
        const double pad = lo == 0.0 ? 1.0 : std::abs(lo) * 0.1;
        lo -= pad;
        hi += pad;
    }
    const double step = niceStep((hi - lo) / 5.0);
    const double first = std::floor(lo / step + 1e-9);
    const double last = std::ceil(hi / step - 1e-9);
    Axis axis;
    axis.lo = first * step;
    axis.hi = last * step;
    for (double k = first; k <= last + 0.5; k += 1.0) {
        axis.ticks.push_back(k * step); // integer multiples: no accumulated rounding
    }
    return axis;
}

Axis logAxis(double lo, double hi) {
    // Candidate ticks 1, 2, 5 × 10^e covering [lo, hi]; the axis spans the closest ticks
    // outside the data.
    const int eLo = static_cast<int>(std::floor(std::log10(lo))) - 1;
    const int eHi = static_cast<int>(std::ceil(std::log10(hi))) + 1;
    std::vector<double> candidates;
    for (int e = eLo; e <= eHi; ++e) {
        for (const double m : {1.0, 2.0, 5.0}) {
            candidates.push_back(m * std::pow(10.0, e));
        }
    }
    Axis axis;
    axis.log = true;
    axis.lo = candidates.front();
    axis.hi = candidates.back();
    for (const double c : candidates) {
        if (c <= lo * (1.0 + 1e-12)) {
            axis.lo = c;
        }
    }
    for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
        if (*it >= hi * (1.0 - 1e-12)) {
            axis.hi = *it;
        }
    }
    if (axis.hi <= axis.lo) {
        axis.hi = axis.lo * 2.0;
    }
    for (const double c : candidates) {
        if (c >= axis.lo * (1.0 - 1e-12) && c <= axis.hi * (1.0 + 1e-12)) {
            axis.ticks.push_back(c);
        }
    }
    // Over many decades the 1-2-5 labels overlap: keep powers of ten only, and over more than
    // ~10 decades every k-th one (a training loss can span 20 decades).
    const int decades = static_cast<int>(std::lround(std::log10(axis.hi / axis.lo)));
    if (decades > 4) {
        const int every = (decades + 9) / 10;
        std::vector<double> sparse;
        for (const double c : axis.ticks) {
            const double e = std::log10(c);
            const long rounded = std::lround(e);
            if (std::abs(e - static_cast<double>(rounded)) < 1e-9 && rounded % every == 0) {
                sparse.push_back(c);
            }
        }
        if (sparse.size() >= 2) {
            axis.ticks = std::move(sparse);
        }
    }
    return axis;
}

Result<void> validateSpec(const PlotSpec& spec) {
    const auto invalid = [](std::string message) {
        return makeError(ErrorCode::InvalidInput, std::move(message),
                         "Fix the data passed to the plot writer.");
    };
    if (spec.series.empty()) {
        return invalid("Plot '" + spec.title + "' has no series");
    }
    if (spec.width < 400 || spec.height < 250) {
        return invalid("Plot '" + spec.title + "' is smaller than 400 x 250 px");
    }
    for (const PlotSeries& s : spec.series) {
        if (s.x.empty() || s.x.size() != s.y.size()) {
            return invalid("Series '" + s.label + "' is empty or has x/y of different length");
        }
        for (std::size_t i = 0; i < s.x.size(); ++i) {
            if (!std::isfinite(s.x[i]) || !std::isfinite(s.y[i])) {
                return invalid("Series '" + s.label + "' has a non-finite point");
            }
            if ((spec.logX && s.x[i] <= 0.0) || (spec.logY && s.y[i] <= 0.0)) {
                return invalid("Series '" + s.label + "' has a value <= 0 on a log axis");
            }
        }
    }
    return {};
}

std::string markerSvg(PlotMarker marker, double x, double y, const std::string& color) {
    const std::string fill = "fill=\"" + color + "\"";
    switch (marker) {
    case PlotMarker::None:
        return {};
    case PlotMarker::Circle:
        return "<circle cx=\"" + num(x) + "\" cy=\"" + num(y) + "\" r=\"3.5\" " + fill + "/>";
    case PlotMarker::Square:
        return "<rect x=\"" + num(x - 3.2) + "\" y=\"" + num(y - 3.2) +
               "\" width=\"6.4\" height=\"6.4\" " + fill + "/>";
    case PlotMarker::Triangle:
        return "<polygon points=\"" + num(x) + "," + num(y - 4.2) + " " + num(x + 4.0) + "," +
               num(y + 3.0) + " " + num(x - 4.0) + "," + num(y + 3.0) + "\" " + fill + "/>";
    case PlotMarker::Diamond:
        return "<polygon points=\"" + num(x) + "," + num(y - 4.5) + " " + num(x + 4.5) + "," +
               num(y) + " " + num(x) + "," + num(y + 4.5) + " " + num(x - 4.5) + "," + num(y) +
               "\" " + fill + "/>";
    }
    return {};
}

std::string strokeAttrs(const PlotSeries& s) {
    std::string a = "fill=\"none\" stroke=\"" + s.color + "\" stroke-width=\"" +
                    num(s.strokeWidth) + "\" stroke-linejoin=\"round\"";
    if (s.dashed) {
        a += " stroke-dasharray=\"7 4\"";
    }
    return a;
}

} // namespace

Result<std::string> renderSvgPlot(const PlotSpec& spec) {
    if (auto valid = validateSpec(spec); !valid) {
        return std::move(valid).error();
    }

    double xMin = std::numeric_limits<double>::infinity();
    double xMax = -xMin;
    double yMin = xMin;
    double yMax = -xMin;
    for (const PlotSeries& s : spec.series) {
        for (std::size_t i = 0; i < s.x.size(); ++i) {
            xMin = std::min(xMin, s.x[i]);
            xMax = std::max(xMax, s.x[i]);
            yMin = std::min(yMin, s.y[i]);
            yMax = std::max(yMax, s.y[i]);
        }
    }
    const Axis ax = spec.logX ? logAxis(xMin, xMax) : linearAxis(xMin, xMax);
    const Axis ay = spec.logY ? logAxis(yMin, yMax) : linearAxis(yMin, yMax);

    const double w = spec.width;
    const double h = spec.height;
    const double left = kMarginLeft;
    const double top = kMarginTop;
    const double plotW = w - kMarginLeft - kMarginRight;
    const double plotH = h - kMarginTop - kMarginBottom;
    const double bottom = top + plotH;
    const double right = left + plotW;
    const auto px = [&](double v) { return left + ax.frac(v) * plotW; };
    const auto py = [&](double v) { return bottom - ay.frac(v) * plotH; };

    std::string svg;
    svg += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg += "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + std::to_string(spec.width) +
           "\" height=\"" + std::to_string(spec.height) + "\" viewBox=\"0 0 " +
           std::to_string(spec.width) + " " + std::to_string(spec.height) + "\" font-family=\"" +
           kFont + "\">\n";
    svg += "<title>" + escapeXml(spec.title) + "</title>\n";
    svg += "<rect width=\"100%\" height=\"100%\" fill=\"" + std::string(plotcolor::kBackground) +
           "\"/>\n";
    svg += "<defs><clipPath id=\"plot-area\"><rect x=\"" + num(left) + "\" y=\"" + num(top) +
           "\" width=\"" + num(plotW) + "\" height=\"" + num(plotH) +
           "\"/></clipPath></defs>\n";

    // Grid and tick labels.
    svg += "<g stroke=\"" + std::string(plotcolor::kGrid) + "\" stroke-width=\"1\">\n";
    for (const double t : ax.ticks) {
        svg += "<line x1=\"" + num(px(t)) + "\" y1=\"" + num(top) + "\" x2=\"" + num(px(t)) +
               "\" y2=\"" + num(bottom) + "\"/>\n";
    }
    for (const double t : ay.ticks) {
        svg += "<line x1=\"" + num(left) + "\" y1=\"" + num(py(t)) + "\" x2=\"" + num(right) +
               "\" y2=\"" + num(py(t)) + "\"/>\n";
    }
    svg += "</g>\n";
    svg += "<g font-size=\"12\" fill=\"" + std::string(plotcolor::kTextSecondary) + "\">\n";
    for (const double t : ax.ticks) {
        svg += "<text x=\"" + num(px(t)) + "\" y=\"" + num(bottom + 18.0) +
               "\" text-anchor=\"middle\">" + tickLabel(t) + "</text>\n";
    }
    for (const double t : ay.ticks) {
        svg += "<text x=\"" + num(left - 8.0) + "\" y=\"" + num(py(t) + 4.0) +
               "\" text-anchor=\"end\">" + tickLabel(t) + "</text>\n";
    }
    svg += "</g>\n";
    svg += "<rect x=\"" + num(left) + "\" y=\"" + num(top) + "\" width=\"" + num(plotW) +
           "\" height=\"" + num(plotH) + "\" fill=\"none\" stroke=\"" + plotcolor::kReference +
           "\" stroke-width=\"1\"/>\n";

    // Title and axis labels.
    svg += "<text x=\"" + num(left) + "\" y=\"28\" font-size=\"16\" font-weight=\"600\" fill=\"" +
           plotcolor::kReference + "\">" + escapeXml(spec.title) + "</text>\n";
    svg += "<text x=\"" + num(left + plotW / 2.0) + "\" y=\"" + num(h - 16.0) +
           "\" font-size=\"13\" text-anchor=\"middle\" fill=\"" + plotcolor::kReference + "\">" +
           escapeXml(spec.xLabel) + "</text>\n";
    svg += "<text transform=\"translate(20 " + num(top + plotH / 2.0) +
           ") rotate(-90)\" font-size=\"13\" text-anchor=\"middle\" fill=\"" +
           plotcolor::kReference + "\">" + escapeXml(spec.yLabel) + "</text>\n";

    // Data, clipped to the plot area.
    svg += "<g clip-path=\"url(#plot-area)\">\n";
    for (const PlotSeries& s : spec.series) {
        if (s.line && s.x.size() > 1) {
            svg += "<polyline " + strokeAttrs(s) + " points=\"";
            for (std::size_t i = 0; i < s.x.size(); ++i) {
                svg += (i == 0 ? "" : " ") + num(px(s.x[i])) + "," + num(py(s.y[i]));
            }
            svg += "\"/>\n";
        }
        for (std::size_t i = 0; i < s.x.size() && s.marker != PlotMarker::None; ++i) {
            svg += markerSvg(s.marker, px(s.x[i]), py(s.y[i]), s.color) + "\n";
        }
    }
    svg += "</g>\n";

    // Legend, right of the plot area.
    const double lx = right + 18.0;
    svg += "<g font-size=\"12\" fill=\"" + std::string(plotcolor::kReference) + "\">\n";
    for (std::size_t i = 0; i < spec.series.size(); ++i) {
        const PlotSeries& s = spec.series[i];
        const double ly = top + 14.0 + 22.0 * static_cast<double>(i);
        if (s.line) {
            svg += "<line x1=\"" + num(lx) + "\" y1=\"" + num(ly) + "\" x2=\"" + num(lx + 28.0) +
                   "\" y2=\"" + num(ly) + "\" " + strokeAttrs(s) + "/>\n";
        }
        if (s.marker != PlotMarker::None) {
            svg += markerSvg(s.marker, lx + 14.0, ly, s.color) + "\n";
        }
        svg += "<text x=\"" + num(lx + 36.0) + "\" y=\"" + num(ly + 4.0) + "\">" +
               escapeXml(s.label) + "</text>\n";
    }
    svg += "</g>\n";
    svg += "</svg>\n";
    return svg;
}

Result<void> saveSvgPlot(const PlotSpec& spec, const std::filesystem::path& path) {
    auto svg = renderSvgPlot(spec);
    if (!svg) {
        return std::move(svg).error();
    }
    return writeFileAtomic(path, svg.value());
}

} // namespace uniforge
