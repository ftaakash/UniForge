#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "core/common/Result.hpp"

namespace uniforge::render {

// Encodes 8-bit RGBA pixels (rows top to bottom, 4 bytes per pixel) as a PNG file in memory.
// Uses uncompressed ("stored") deflate blocks: larger files than zlib, but ~60 lines with no
// third-party code, and a plot screenshot is still only a few MB.
[[nodiscard]] std::vector<std::uint8_t> encodePng(const std::uint8_t* rgba, std::uint32_t width,
                                                  std::uint32_t height);

// encodePng + atomic save (temp file, then rename). IoError names the file on failure.
Result<void> savePng(const std::filesystem::path& file, const std::uint8_t* rgba,
                     std::uint32_t width, std::uint32_t height);

// CRC-32 (PNG/zlib polynomial) and Adler-32, exposed for the unit test.
[[nodiscard]] std::uint32_t crc32(const std::uint8_t* data, std::size_t size,
                                  std::uint32_t crc = 0);
[[nodiscard]] std::uint32_t adler32(const std::uint8_t* data, std::size_t size);

} // namespace uniforge::render
