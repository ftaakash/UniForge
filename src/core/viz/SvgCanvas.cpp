#include "core/viz/SvgCanvas.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string_view>

#include "core/common/AtomicFile.hpp"
#include "core/viz/Colormap.hpp"

namespace uniforge::viz {

namespace {

constexpr const char* kTextFont = "Inter, 'Segoe UI', Arial, sans-serif";
constexpr const char* kMonoFont = "'JetBrains Mono', Consolas, monospace";

std::string num(double v) {
    char buffer[32];
    const int n = std::snprintf(buffer, sizeof buffer, "%.2f", v);
    return std::string(buffer, n > 0 ? static_cast<std::size_t>(n) : 0U);
}

} // namespace

std::string base64(const std::vector<std::uint8_t>& data) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const std::uint32_t v = (std::uint32_t{data[i]} << 16) | (std::uint32_t{data[i + 1]} << 8) |
                                std::uint32_t{data[i + 2]};
        out += kAlphabet[(v >> 18) & 63U];
        out += kAlphabet[(v >> 12) & 63U];
        out += kAlphabet[(v >> 6) & 63U];
        out += kAlphabet[v & 63U];
    }
    if (i < data.size()) {
        std::uint32_t v = std::uint32_t{data[i]} << 16;
        if (i + 1 < data.size()) {
            v |= std::uint32_t{data[i + 1]} << 8;
        }
        out += kAlphabet[(v >> 18) & 63U];
        out += kAlphabet[(v >> 12) & 63U];
        out += i + 1 < data.size() ? kAlphabet[(v >> 6) & 63U] : '=';
        out += '=';
    }
    return out;
}

std::string escapeXmlText(const std::string& text) {
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

std::string labelNumber(double v, int digits) {
    char buffer[48];
    if (v != 0.0 && (std::fabs(v) >= 1e6 || std::fabs(v) < 1e-3)) {
        std::snprintf(buffer, sizeof buffer, "%.*e", std::max(0, digits - 1), v);
    } else {
        std::snprintf(buffer, sizeof buffer, "%.*g", digits, v);
    }
    return buffer;
}

SvgCanvas::SvgCanvas(int width, int height, const std::string& title)
    : width_(width)
    , height_(height)
    , title_(title) {}

// Placeholder for the image href, resolved in str() once the file stem is known. '\x01'
// cannot occur in escaped SVG text.
constexpr char kImageMark = '\x01';

void SvgCanvas::image(const std::vector<std::uint8_t>& png, double x, double y, double w,
                      double h) {
    images_.push_back(png);
    body_ += "<image x=\"" + num(x) + "\" y=\"" + num(y) + "\" width=\"" + num(w) + "\" height=\"" +
             num(h) + "\" href=\"" + kImageMark + "\"/>\n";
}

std::string SvgCanvas::imageName(const std::string& stem, std::size_t k) {
    return stem + "-" + std::to_string(k) + ".png";
}

void SvgCanvas::text(double x, double y, const std::string& text, double size,
                     const std::string& anchor, const std::string& color, bool bold, bool mono) {
    body_ += "<text x=\"" + num(x) + "\" y=\"" + num(y) + "\" font-size=\"" + num(size) +
             "\" text-anchor=\"" + anchor + "\" fill=\"" + color + "\"" +
             (bold ? " font-weight=\"600\"" : "") +
             (mono ? std::string(" font-family=\"") + kMonoFont + "\"" : std::string()) + ">" +
             escapeXmlText(text) + "</text>\n";
}

void SvgCanvas::line(double x1, double y1, double x2, double y2, const std::string& color,
                     double width, bool dashed) {
    body_ += "<line x1=\"" + num(x1) + "\" y1=\"" + num(y1) + "\" x2=\"" + num(x2) + "\" y2=\"" +
             num(y2) + "\" stroke=\"" + color + "\" stroke-width=\"" + num(width) + "\"" +
             (dashed ? " stroke-dasharray=\"6 4\"" : "") + "/>\n";
}

void SvgCanvas::rect(double x, double y, double w, double h, const std::string& fill,
                     const std::string& stroke, double strokeWidth, double radius) {
    body_ += "<rect x=\"" + num(x) + "\" y=\"" + num(y) + "\" width=\"" + num(w) + "\" height=\"" +
             num(h) + "\" fill=\"" + fill + "\" stroke=\"" + stroke + "\" stroke-width=\"" +
             num(strokeWidth) + "\"" +
             (radius > 0.0 ? " rx=\"" + num(radius) + "\"" : std::string()) + "/>\n";
}

void SvgCanvas::circle(double cx, double cy, double r, const std::string& fill,
                       const std::string& stroke) {
    body_ += "<circle cx=\"" + num(cx) + "\" cy=\"" + num(cy) + "\" r=\"" + num(r) + "\" fill=\"" +
             fill + "\" stroke=\"" + stroke + "\" stroke-width=\"1.5\"/>\n";
}

void SvgCanvas::polygon(const std::vector<std::array<double, 2>>& points, const std::string& fill,
                        const std::string& stroke) {
    body_ += "<polygon points=\"";
    for (std::size_t i = 0; i < points.size(); ++i) {
        body_ += (i == 0 ? "" : " ") + num(points[i][0]) + "," + num(points[i][1]);
    }
    body_ += "\" fill=\"" + fill + "\" stroke=\"" + stroke + "\" stroke-width=\"1.2\"/>\n";
}

void SvgCanvas::arrow(double x1, double y1, double x2, double y2, const std::string& color,
                      double width) {
    const double dx = x2 - x1, dy = y2 - y1;
    const double len = std::hypot(dx, dy);
    if (len < 1e-9) {
        return;
    }
    const double ux = dx / len, uy = dy / len;
    const double head = 7.0 + 2.0 * width;
    const double bx = x2 - ux * head, by = y2 - uy * head;
    line(x1, y1, bx, by, color, width);
    polygon({{x2, y2},
             {bx - uy * head * 0.45, by + ux * head * 0.45},
             {bx + uy * head * 0.45, by - ux * head * 0.45}},
            color);
}

void SvgCanvas::colorbar(double x, double y, double w, double h, double lo, double hi,
                         const std::string& label, int ticks) {
    constexpr int kSteps = 64;
    for (int i = 0; i < kSteps; ++i) {
        const double t = 1.0 - (i + 0.5) / kSteps; // top = hot
        rect(x, y + h * i / kSteps, w, h / kSteps + 0.6, toHex(inferno(t)));
    }
    rect(x, y, w, h, "none", "#1A1E24", 0.8);
    for (int k = 0; k < ticks; ++k) {
        const double f = static_cast<double>(k) / (ticks - 1);
        const double ty = y + h * (1.0 - f);
        line(x + w, ty, x + w + 5.0, ty, "#1A1E24", 0.8);
        text(x + w + 8.0, ty + 4.0, labelNumber(lo + f * (hi - lo), 4), 11.0, "start", "#1A1E24",
             false, true);
    }
    text(x + w * 0.5, y - 10.0, label, 12.0, "middle", "#1A1E24", true);
}

void SvgCanvas::textBlock(double x, double y, const std::string& text, double size,
                          double lineHeight, const std::string& color) {
    std::size_t start = 0;
    int lineNo = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::string piece =
            text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        this->text(x, y + lineNo * size * lineHeight, piece, size, "start", color);
        ++lineNo;
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
}

void SvgCanvas::raw(const std::string& svg) {
    body_ += svg;
}

std::string SvgCanvas::str(const std::string& imageStem) const {
    std::string svg = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg += "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + std::to_string(width_) +
           "\" height=\"" + std::to_string(height_) + "\" viewBox=\"0 0 " + std::to_string(width_) +
           " " + std::to_string(height_) + "\" font-family=\"" + kTextFont + "\">\n";
    svg += "<title>" + escapeXmlText(title_) + "</title>\n";
    svg += "<rect width=\"100%\" height=\"100%\" fill=\"#FFFFFF\"/>\n";
    std::size_t k = 0;
    for (const char ch : body_) {
        if (ch == kImageMark) {
            svg += escapeXmlText(imageName(imageStem, ++k));
        } else {
            svg += ch;
        }
    }
    svg += "</svg>\n";
    return svg;
}

Result<void> SvgCanvas::save(const std::filesystem::path& path) const {
    const std::string stem = path.stem().u8string();
    for (std::size_t k = 0; k < images_.size(); ++k) {
        const auto& png = images_[k];
        const std::string_view bytes(reinterpret_cast<const char*>(png.data()), png.size());
        if (auto r = writeFileAtomic(path.parent_path() / imageName(stem, k + 1), bytes); !r) {
            return r;
        }
    }
    return writeFileAtomic(path, str(stem));
}

} // namespace uniforge::viz
