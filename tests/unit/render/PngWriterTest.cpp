#include "app/render/PngWriter.hpp"

#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace uniforge::render {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) |
           std::uint32_t{p[3]};
}

TEST(PngWriter, ChecksumsMatchKnownValues) {
    const std::string s = "123456789";
    const auto* d = reinterpret_cast<const std::uint8_t*>(s.data());
    EXPECT_EQ(crc32(d, s.size()), 0xCBF43926u);   // standard CRC-32 check value
    EXPECT_EQ(adler32(d, s.size()), 0x091E01DEu); // standard Adler-32 check value
}

TEST(PngWriter, WritesSignatureHeaderAndValidChunkCrcs) {
    std::vector<std::uint8_t> rgba(3 * 2 * 4, 0x7F);
    const auto png = encodePng(rgba.data(), 3, 2);
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

TEST(PngWriter, SplitsLargeImagesIntoStoredBlocks) {
    // 200 × 100 RGBA = 80 100 raw bytes: two stored deflate blocks (max 65 535 each).
    std::vector<std::uint8_t> rgba(200 * 100 * 4, 0x11);
    const auto png = encodePng(rgba.data(), 200, 100);
    EXPECT_GT(png.size(), 80100u + 2 * 5);
}

} // namespace
} // namespace uniforge::render
