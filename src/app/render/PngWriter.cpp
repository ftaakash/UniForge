#include "app/render/PngWriter.hpp"

#include <algorithm>
#include <array>
#include <string_view>

#include "core/common/AtomicFile.hpp"

namespace uniforge::render {

namespace {

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void chunk(std::vector<std::uint8_t>& out, const char type[4],
           const std::vector<std::uint8_t>& data) {
    put32(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t typeAt = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32(out, crc32(out.data() + typeAt, data.size() + 4));
}

} // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1U) ? 0xEDB88320U ^ (c >> 1) : c >> 1;
            }
            t[n] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8);
    }
    return ~crc;
}

std::uint32_t adler32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t a = 1, b = 0;
    for (std::size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % 65521U;
        b = (b + a) % 65521U;
    }
    return (b << 16) | a;
}

std::vector<std::uint8_t> encodePng(const std::uint8_t* rgba, std::uint32_t width,
                                    std::uint32_t height) {
    // Raw scanlines: filter byte 0 (None) + RGBA row.
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
    std::vector<std::uint8_t> raw;
    raw.reserve((rowBytes + 1) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + y * rowBytes, rgba + (y + 1) * rowBytes);
    }

    // zlib stream of stored deflate blocks (at most 65535 bytes each).
    std::vector<std::uint8_t> z{0x78, 0x01};
    std::size_t pos = 0;
    do {
        const std::size_t len = std::min<std::size_t>(65535, raw.size() - pos);
        const bool last = pos + len == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(len & 0xFF));
        z.push_back(static_cast<std::uint8_t>(len >> 8));
        z.push_back(static_cast<std::uint8_t>(~len & 0xFF));
        z.push_back(static_cast<std::uint8_t>((~len >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                 raw.begin() + static_cast<std::ptrdiff_t>(pos + len));
        pos += len;
    } while (pos < raw.size());
    put32(z, adler32(raw.data(), raw.size()));

    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> ihdr;
    put32(ihdr, width);
    put32(ihdr, height);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0}); // 8-bit, RGBA, deflate, no filter set, no interlace
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});
    return png;
}

Result<void> savePng(const std::filesystem::path& file, const std::uint8_t* rgba,
                     std::uint32_t width, std::uint32_t height) {
    const auto png = encodePng(rgba, width, height);
    return writeFileAtomic(file,
                           std::string_view(reinterpret_cast<const char*>(png.data()), png.size()));
}

} // namespace uniforge::render
