#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "core/common/Result.hpp"

namespace uniforge::viz {

// PNG writer (moved from app/render in V2-9, build spec §4.1). Encodes 8-bit RGBA pixels (rows
// top to bottom, 4 bytes per pixel). Each row gets the PNG filter (None, Sub, Up, Average,
// Paeth) with the smallest sum of absolute values, and the zlib stream is one deflate block
// with the fixed Huffman code and LZ77 matches (32 KiB window). No third-party code; a flat
// report render compresses to a few percent of its raw size, so the PDF stays small (build
// spec §12.4). `compress = false` keeps v1's stored (uncompressed) blocks.
struct PngOptions {
    bool compress = true;
};

[[nodiscard]] std::vector<std::uint8_t> encodePng(const std::uint8_t* rgba, std::uint32_t width,
                                                  std::uint32_t height, PngOptions options = {});

// encodePng + atomic save (temp file, then rename). IoError names the file on failure.
Result<void> savePng(const std::filesystem::path& file, const std::uint8_t* rgba,
                     std::uint32_t width, std::uint32_t height, PngOptions options = {});

// zlib stream (RFC 1950) of `data`: one fixed-Huffman deflate block (RFC 1951) or, without
// compression, stored blocks. Exposed for the unit test, which inflates it again.
[[nodiscard]] std::vector<std::uint8_t> zlibCompress(const std::uint8_t* data, std::size_t size,
                                                     bool compress);

// CRC-32 (PNG/zlib polynomial) and Adler-32, exposed for the unit test.
[[nodiscard]] std::uint32_t crc32(const std::uint8_t* data, std::size_t size,
                                  std::uint32_t crc = 0);
[[nodiscard]] std::uint32_t adler32(const std::uint8_t* data, std::size_t size);

} // namespace uniforge::viz
