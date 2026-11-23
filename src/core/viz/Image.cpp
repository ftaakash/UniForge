#include "core/viz/Image.hpp"

#include <algorithm>
#include <cstdio>

namespace uniforge::viz {

namespace {

int hexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

} // namespace

Rgb hexColor(const std::string& hex) {
    if (hex.size() != 7 || hex[0] != '#') {
        return {0, 0, 0};
    }
    Rgb c{};
    for (std::size_t k = 0; k < 3; ++k) {
        const int hi = hexDigit(hex[1 + 2 * k]);
        const int lo = hexDigit(hex[2 + 2 * k]);
        if (hi < 0 || lo < 0) {
            return {0, 0, 0};
        }
        c[k] = static_cast<std::uint8_t>(hi * 16 + lo);
    }
    return c;
}

std::string toHex(const Rgb& c) {
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "#%02X%02X%02X", c[0], c[1], c[2]);
    return buffer;
}

Image::Image(int w, int h, const Rgb& background)
    : width(w)
    , height(h)
    , rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4) {
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = background[0];
        rgba[i + 1] = background[1];
        rgba[i + 2] = background[2];
        rgba[i + 3] = 255;
    }
}

void Image::set(int x, int y, const Rgb& c) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return;
    }
    const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                            static_cast<std::size_t>(x)) *
                           4;
    rgba[at] = c[0];
    rgba[at + 1] = c[1];
    rgba[at + 2] = c[2];
    rgba[at + 3] = 255;
}

Rgb Image::get(int x, int y) const {
    const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                            static_cast<std::size_t>(x)) *
                           4;
    return {rgba[at], rgba[at + 1], rgba[at + 2]};
}

void Image::fillRect(int x0, int y0, int x1, int y1, const Rgb& c) {
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, width);
    y1 = std::min(y1, height);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            set(x, y, c);
        }
    }
}

Image Image::downsample(int factor) const {
    if (factor <= 1) {
        return *this;
    }
    Image out(width / factor, height / factor, {0, 0, 0});
    const int n = factor * factor;
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            int sum[3] = {0, 0, 0};
            for (int dy = 0; dy < factor; ++dy) {
                for (int dx = 0; dx < factor; ++dx) {
                    const Rgb c = get(x * factor + dx, y * factor + dy);
                    sum[0] += c[0];
                    sum[1] += c[1];
                    sum[2] += c[2];
                }
            }
            out.set(x, y,
                    {static_cast<std::uint8_t>((sum[0] + n / 2) / n),
                     static_cast<std::uint8_t>((sum[1] + n / 2) / n),
                     static_cast<std::uint8_t>((sum[2] + n / 2) / n)});
        }
    }
    return out;
}

} // namespace uniforge::viz
