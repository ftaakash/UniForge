#include "core/viz/PngWriter.hpp"

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace uniforge::viz {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) |
           std::uint32_t{p[3]};
}

// A small independent inflater (stored and fixed-Huffman blocks, RFC 1951), so the test checks
// the encoder against the format and not against itself.
class Inflater {
public:
    explicit Inflater(const std::vector<std::uint8_t>& z)
        : z_(z) {}

    std::vector<std::uint8_t> run() {
        if (z_.size() < 6 || ((z_[0] << 8) | z_[1]) % 31 != 0 || (z_[0] & 0x0F) != 8) {
            throw std::runtime_error("bad zlib header");
        }
        pos_ = 2;
        std::vector<std::uint8_t> out;
        bool last = false;
        while (!last) {
            last = bits(1) == 1;
            const std::uint32_t type = bits(2);
            if (type == 0) {
                bit_ = 0; // byte align
                ++pos_;
                const std::uint32_t len = z_[pos_] | (z_[pos_ + 1] << 8);
                const std::uint32_t nlen = z_[pos_ + 2] | (z_[pos_ + 3] << 8);
                if ((len ^ 0xFFFFU) != nlen) {
                    throw std::runtime_error("bad stored length");
                }
                pos_ += 4;
                out.insert(out.end(), z_.begin() + static_cast<std::ptrdiff_t>(pos_),
                           z_.begin() + static_cast<std::ptrdiff_t>(pos_ + len));
                pos_ += len;
            } else if (type == 1) {
                fixedBlock(out);
            } else {
                throw std::runtime_error("unsupported block type");
            }
        }
        return out;
    }

private:
    std::uint32_t bits(int n) {
        std::uint32_t v = 0;
        for (int i = 0; i < n; ++i) {
            if (pos_ >= z_.size()) {
                throw std::runtime_error("out of data");
            }
            v |= static_cast<std::uint32_t>((z_[pos_] >> bit_) & 1U) << i;
            if (++bit_ == 8) {
                bit_ = 0;
                ++pos_;
            }
        }
        return v;
    }
    std::uint32_t huff(int n) { // n bits, most significant first
        std::uint32_t v = 0;
        for (int i = 0; i < n; ++i) {
            v = (v << 1) | bits(1);
        }
        return v;
    }
    int literal() {
        std::uint32_t v = huff(7);
        if (v <= 0x17) {
            return static_cast<int>(256 + v);
        }
        v = (v << 1) | bits(1);
        if (v >= 0x30 && v <= 0xBF) {
            return static_cast<int>(v - 0x30);
        }
        if (v >= 0xC0 && v <= 0xC7) {
            return static_cast<int>(280 + v - 0xC0);
        }
        v = (v << 1) | bits(1);
        return static_cast<int>(144 + v - 0x190);
    }
    void fixedBlock(std::vector<std::uint8_t>& out) {
        static const int lb[] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const int le[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const int db[] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                 33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const int de[] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            const int sym = literal();
            if (sym < 256) {
                out.push_back(static_cast<std::uint8_t>(sym));
                continue;
            }
            if (sym == 256) {
                return;
            }
            const int len = lb[sym - 257] + static_cast<int>(bits(le[sym - 257]));
            const auto dc = static_cast<int>(huff(5));
            const int dist = db[dc] + static_cast<int>(bits(de[dc]));
            if (dist > static_cast<int>(out.size())) {
                throw std::runtime_error("distance too far back");
            }
            for (int k = 0; k < len; ++k) {
                out.push_back(out[out.size() - static_cast<std::size_t>(dist)]);
            }
        }
    }

    const std::vector<std::uint8_t>& z_;
    std::size_t pos_ = 0;
    int bit_ = 0;
};

TEST(PngWriter, ChecksumsMatchKnownValues) {
    const std::string s = "123456789";
    const auto* d = reinterpret_cast<const std::uint8_t*>(s.data());
    EXPECT_EQ(crc32(d, s.size()), 0xCBF43926u);   // standard CRC-32 check value
    EXPECT_EQ(adler32(d, s.size()), 0x091E01DEu); // standard Adler-32 check value
}

TEST(PngWriter, WritesSignatureHeaderAndValidChunkCrcs) {
    std::vector<std::uint8_t> rgba(3 * 2 * 4, 0x7F);
    for (const bool compress : {false, true}) {
        const auto png = encodePng(rgba.data(), 3, 2, {compress});
        const std::uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        ASSERT_GT(png.size(), 8u);
        EXPECT_EQ(std::memcmp(png.data(), signature, 8), 0);
        std::size_t pos = 8;
        std::vector<std::string> types;
        while (pos + 12 <= png.size()) {
            const std::uint32_t len = be32(&png[pos]);
            types.emplace_back(reinterpret_cast<const char*>(&png[pos + 4]), 4);
            EXPECT_EQ(be32(&png[pos + 8 + len]), crc32(&png[pos + 4], len + 4)) << types.back();
            if (types.back() == "IHDR") {
                EXPECT_EQ(be32(&png[pos + 8]), 3u);
                EXPECT_EQ(be32(&png[pos + 12]), 2u);
                EXPECT_EQ(png[pos + 16], 8); // bit depth
                EXPECT_EQ(png[pos + 17], 6); // RGBA
            }
            pos += 12 + len;
        }
        EXPECT_EQ(pos, png.size());
        EXPECT_EQ(types, (std::vector<std::string>{"IHDR", "IDAT", "IEND"}));
    }
}

TEST(PngWriter, CompressedStreamInflatesToTheInput) {
    // Text with repeats, a long run, and pseudo-random bytes (no matches): all three paths.
    std::vector<std::uint8_t> data;
    const std::string text = "the quick brown fox jumps over the lazy dog; the quick brown fox ";
    for (int r = 0; r < 40; ++r) {
        data.insert(data.end(), text.begin(), text.end());
    }
    data.insert(data.end(), 70000, 0x42); // longer than the window and than one match
    std::uint32_t state = 12345;
    for (int i = 0; i < 5000; ++i) {
        state = state * 1103515245U + 12345U;
        data.push_back(static_cast<std::uint8_t>(state >> 24));
    }
    for (const bool compress : {false, true}) {
        const auto z = zlibCompress(data.data(), data.size(), compress);
        const auto back = Inflater(z).run();
        ASSERT_EQ(back.size(), data.size()) << "compress=" << compress;
        EXPECT_TRUE(back == data) << "compress=" << compress;
        const std::size_t n = z.size();
        EXPECT_EQ((std::uint32_t{z[n - 4]} << 24) | (std::uint32_t{z[n - 3]} << 16) |
                      (std::uint32_t{z[n - 2]} << 8) | std::uint32_t{z[n - 1]},
                  adler32(data.data(), data.size()));
    }
    const auto compressed = zlibCompress(data.data(), data.size(), true);
    EXPECT_LT(compressed.size(), data.size() / 5); // repeats and the long run collapse
}

TEST(PngWriter, FlatRenderCompressesStronglyAndUnfiltersBack) {
    // 400 × 300 white image with a grey gradient band: filters + LZ77 shrink it a lot.
    const std::uint32_t w = 400, h = 300;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4, 255);
    for (std::uint32_t y = 100; y < 200; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * w + x) * 4;
            rgba[at] = rgba[at + 1] = rgba[at + 2] = static_cast<std::uint8_t>(x * 255 / w);
        }
    }
    const auto png = encodePng(rgba.data(), w, h);
    EXPECT_LT(png.size(), rgba.size() / 20);
    // Inflate IDAT and undo the filters: the pixels must come back exactly.
    std::size_t pos = 8 + 12 + 13;
    const std::uint32_t len = be32(&png[pos]);
    const std::vector<std::uint8_t> z(png.begin() + static_cast<std::ptrdiff_t>(pos + 8),
                                      png.begin() + static_cast<std::ptrdiff_t>(pos + 8 + len));
    const auto raw = Inflater(z).run();
    const std::size_t rowBytes = static_cast<std::size_t>(w) * 4;
    ASSERT_EQ(raw.size(), (rowBytes + 1) * h);
    std::vector<std::uint8_t> out(rgba.size());
    for (std::size_t y = 0; y < h; ++y) {
        const std::uint8_t type = raw[y * (rowBytes + 1)];
        ASSERT_LE(type, 4);
        for (std::size_t x = 0; x < rowBytes; ++x) {
            const int a = x >= 4 ? out[y * rowBytes + x - 4] : 0;
            const int b = y > 0 ? out[(y - 1) * rowBytes + x] : 0;
            const int c = (x >= 4 && y > 0) ? out[(y - 1) * rowBytes + x - 4] : 0;
            int pred = 0;
            if (type == 1) {
                pred = a;
            } else if (type == 2) {
                pred = b;
            } else if (type == 3) {
                pred = (a + b) / 2;
            } else if (type == 4) {
                const int p = a + b - c;
                const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
                pred = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
            }
            out[y * rowBytes + x] =
                static_cast<std::uint8_t>(raw[y * (rowBytes + 1) + 1 + x] + pred);
        }
    }
    EXPECT_TRUE(out == rgba);
}

} // namespace
} // namespace uniforge::viz
