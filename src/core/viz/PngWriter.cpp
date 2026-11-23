#include "core/viz/PngWriter.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string_view>

#include "core/common/AtomicFile.hpp"

namespace uniforge::viz {

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

// Bits are packed least-significant first (RFC 1951 §3.1.1).
class BitWriter {
public:
    explicit BitWriter(std::vector<std::uint8_t>& out)
        : out_(out) {}
    void bits(std::uint32_t value, int count) {
        for (int i = 0; i < count; ++i) {
            acc_ |= ((value >> i) & 1U) << fill_;
            if (++fill_ == 8) {
                out_.push_back(static_cast<std::uint8_t>(acc_));
                acc_ = 0;
                fill_ = 0;
            }
        }
    }
    // Huffman codes are sent most-significant bit first.
    void code(std::uint32_t value, int count) {
        for (int i = count - 1; i >= 0; --i) {
            bits((value >> i) & 1U, 1);
        }
    }
    void flush() {
        if (fill_ > 0) {
            out_.push_back(static_cast<std::uint8_t>(acc_));
            acc_ = 0;
            fill_ = 0;
        }
    }

private:
    std::vector<std::uint8_t>& out_;
    std::uint32_t acc_ = 0;
    int fill_ = 0;
};

// Fixed literal/length code (RFC 1951 §3.2.6).
void literalCode(BitWriter& w, int symbol) {
    if (symbol <= 143) {
        w.code(static_cast<std::uint32_t>(0x30 + symbol), 8);
    } else if (symbol <= 255) {
        w.code(static_cast<std::uint32_t>(0x190 + symbol - 144), 9);
    } else if (symbol <= 279) {
        w.code(static_cast<std::uint32_t>(symbol - 256), 7);
    } else {
        w.code(static_cast<std::uint32_t>(0xC0 + symbol - 280), 8);
    }
}

constexpr std::array<int, 29> kLengthBase = {3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                             15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                             67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<int, 29> kLengthExtra = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                              2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<int, 30> kDistBase = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<int, 30> kDistExtra = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                            6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void emitMatch(BitWriter& w, int length, int distance) {
    int lc = 28;
    while (kLengthBase[static_cast<std::size_t>(lc)] > length) {
        --lc;
    }
    literalCode(w, 257 + lc);
    w.bits(static_cast<std::uint32_t>(length - kLengthBase[static_cast<std::size_t>(lc)]),
           kLengthExtra[static_cast<std::size_t>(lc)]);
    int dc = 29;
    while (kDistBase[static_cast<std::size_t>(dc)] > distance) {
        --dc;
    }
    w.code(static_cast<std::uint32_t>(dc), 5);
    w.bits(static_cast<std::uint32_t>(distance - kDistBase[static_cast<std::size_t>(dc)]),
           kDistExtra[static_cast<std::size_t>(dc)]);
}

// One fixed-Huffman block with greedy LZ77 over hash chains of 3-byte prefixes.
void deflateFixed(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out) {
    constexpr std::size_t kWindow = 32768;
    constexpr int kMaxMatch = 258;
    constexpr int kMinMatch = 3;
    constexpr int kMaxChain = 48;
    constexpr std::size_t kHashSize = std::size_t(1) << 15;
    std::vector<std::int64_t> head(kHashSize, -1);
    std::vector<std::int64_t> prev(kWindow, -1);
    const auto hash = [&](std::size_t i) {
        const std::uint32_t v = (std::uint32_t{data[i]} << 16) | (std::uint32_t{data[i + 1]} << 8) |
                                std::uint32_t{data[i + 2]};
        return static_cast<std::size_t>((v * 2654435761U) >> 17) & (kHashSize - 1);
    };
    const auto insert = [&](std::size_t i) {
        if (i + 2 < size) {
            const std::size_t hv = hash(i);
            prev[i % kWindow] = head[hv];
            head[hv] = static_cast<std::int64_t>(i);
        }
    };

    BitWriter w(out);
    w.bits(1, 1); // BFINAL
    w.bits(1, 2); // BTYPE = 01, fixed Huffman
    std::size_t i = 0;
    while (i < size) {
        int bestLen = 0;
        std::size_t bestDist = 0;
        if (i + 2 < size) {
            std::int64_t cand = head[hash(i)];
            const std::size_t limit = std::min<std::size_t>(kMaxMatch, size - i);
            for (int chain = 0; cand >= 0 && chain < kMaxChain; ++chain) {
                const auto c = static_cast<std::size_t>(cand);
                if (i - c > kWindow - 1) {
                    break;
                }
                std::size_t len = 0;
                while (len < limit && data[c + len] == data[i + len]) {
                    ++len;
                }
                if (static_cast<int>(len) > bestLen) {
                    bestLen = static_cast<int>(len);
                    bestDist = i - c;
                    if (len == limit) {
                        break;
                    }
                }
                const std::int64_t next = prev[c % kWindow];
                if (next >= cand) {
                    break; // the slot was overwritten by a newer position
                }
                cand = next;
            }
        }
        if (bestLen >= kMinMatch) {
            emitMatch(w, bestLen, static_cast<int>(bestDist));
            for (int k = 0; k < bestLen; ++k) {
                insert(i + static_cast<std::size_t>(k));
            }
            i += static_cast<std::size_t>(bestLen);
        } else {
            literalCode(w, data[i]);
            insert(i);
            ++i;
        }
    }
    literalCode(w, 256); // end of block
    w.flush();
}

void deflateStored(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& z) {
    std::size_t pos = 0;
    do {
        const std::size_t len = std::min<std::size_t>(65535, size - pos);
        const bool last = pos + len == size;
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(len & 0xFF));
        z.push_back(static_cast<std::uint8_t>(len >> 8));
        z.push_back(static_cast<std::uint8_t>(~len & 0xFF));
        z.push_back(static_cast<std::uint8_t>((~len >> 8) & 0xFF));
        z.insert(z.end(), data + pos, data + pos + len);
        pos += len;
    } while (pos < size);
}

int paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) {
        return a;
    }
    return pb <= pc ? b : c;
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

std::vector<std::uint8_t> zlibCompress(const std::uint8_t* data, std::size_t size, bool compress) {
    std::vector<std::uint8_t> z{0x78, 0x01};
    if (compress) {
        deflateFixed(data, size, z);
    } else {
        deflateStored(data, size, z);
    }
    put32(z, adler32(data, size));
    return z;
}

std::vector<std::uint8_t> encodePng(const std::uint8_t* rgba, std::uint32_t width,
                                    std::uint32_t height, PngOptions options) {
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
    std::vector<std::uint8_t> raw;
    raw.reserve((rowBytes + 1) * height);
    std::vector<std::uint8_t> best(rowBytes);
    std::vector<std::uint8_t> trial(rowBytes);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* row = rgba + static_cast<std::size_t>(y) * rowBytes;
        const std::uint8_t* up = y > 0 ? row - rowBytes : nullptr;
        if (!options.compress) {
            raw.push_back(0);
            raw.insert(raw.end(), row, row + rowBytes);
            continue;
        }
        long bestCost = -1;
        std::uint8_t bestType = 0;
        for (std::uint8_t type = 0; type < 5; ++type) {
            long cost = 0;
            for (std::size_t x = 0; x < rowBytes; ++x) {
                const int a = x >= 4 ? row[x - 4] : 0;
                const int b = up != nullptr ? up[x] : 0;
                const int c = (x >= 4 && up != nullptr) ? up[x - 4] : 0;
                int pred = 0;
                switch (type) {
                case 1:
                    pred = a;
                    break;
                case 2:
                    pred = b;
                    break;
                case 3:
                    pred = (a + b) / 2;
                    break;
                case 4:
                    pred = paeth(a, b, c);
                    break;
                default:
                    break;
                }
                const auto v = static_cast<std::uint8_t>(row[x] - pred);
                trial[x] = v;
                cost += v < 128 ? v : 256 - v;
            }
            if (bestCost < 0 || cost < bestCost) {
                bestCost = cost;
                bestType = type;
                best.swap(trial);
            }
        }
        raw.push_back(bestType);
        raw.insert(raw.end(), best.begin(), best.end());
    }
    const std::vector<std::uint8_t> z = zlibCompress(raw.data(), raw.size(), options.compress);

    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> ihdr;
    put32(ihdr, width);
    put32(ihdr, height);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0}); // 8-bit, RGBA, deflate, adaptive filter, no interlace
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});
    return png;
}

Result<void> savePng(const std::filesystem::path& file, const std::uint8_t* rgba,
                     std::uint32_t width, std::uint32_t height, PngOptions options) {
    const auto png = encodePng(rgba, width, height, options);
    return writeFileAtomic(file,
                           std::string_view(reinterpret_cast<const char*>(png.data()), png.size()));
}

} // namespace uniforge::viz
