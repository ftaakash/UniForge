// Checks that the embedded fonts contain every glyph listed in src/app/ui/GlyphRanges.hpp, so
// unit labels such as "°C" or "≤ 1e-08 ✓" never render as '?' (spec §8.2, risk table §16).

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "app/ui/GlyphRanges.hpp"
#include "imstb_truetype.h"

namespace {

struct Font {
    std::vector<unsigned char> bytes;
    stbtt_fontinfo info{};
};

Font loadFont(const char* fileName) {
    Font font;
    const std::string path = std::string(UNIFORGE_FONTS_DIR) + "/" + fileName;
    std::ifstream in(path, std::ios::binary);
    font.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (font.bytes.empty() || !stbtt_InitFont(&font.info, font.bytes.data(),
                                              stbtt_GetFontOffsetForIndex(font.bytes.data(), 0))) {
        ADD_FAILURE() << "cannot load font " << path;
        font.bytes.clear();
    }
    return font;
}

bool hasGlyph(const Font& font, unsigned int codepoint) {
    return !font.bytes.empty() &&
           stbtt_FindGlyphIndex(&font.info, static_cast<int>(codepoint)) != 0;
}

// Returns the codepoints in `ranges` that none of `chain` covers, as "U+XXXX" strings.
std::vector<std::string> missing(const unsigned int* ranges,
                                 const std::vector<const Font*>& chain) {
    std::vector<std::string> out;
    for (; ranges[0] != 0; ranges += 2) {
        for (unsigned int c = ranges[0]; c <= ranges[1]; ++c) {
            bool found = false;
            for (const Font* f : chain) {
                found = found || hasGlyph(*f, c);
            }
            if (!found) {
                char buf[16];
                std::snprintf(buf, sizeof buf, "U+%04X", c);
                out.emplace_back(buf);
            }
        }
    }
    return out;
}

std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& item : v) {
        s += item + " ";
    }
    return s;
}

} // namespace

// The UI font is Inter with JetBrains Mono merged in as the fallback (see ui/Fonts.cpp).
TEST(FontCoverage, UiFontChainCoversRequiredGlyphs) {
    const Font inter = loadFont("Inter-Regular.ttf");
    const Font interSemi = loadFont("Inter-SemiBold.ttf");
    const Font mono = loadFont("JetBrainsMono-Regular.ttf");
    const auto regularGaps = missing(uniforge::glyphs::kUiRanges, {&inter, &mono});
    const auto semiBoldGaps = missing(uniforge::glyphs::kUiRanges, {&interSemi, &mono});
    EXPECT_TRUE(regularGaps.empty()) << "missing: " << join(regularGaps);
    EXPECT_TRUE(semiBoldGaps.empty()) << "missing: " << join(semiBoldGaps);
}

TEST(FontCoverage, MonoFontCoversRequiredGlyphs) {
    const Font mono = loadFont("JetBrainsMono-Regular.ttf");
    const auto gaps = missing(uniforge::glyphs::kMonoRanges, {&mono});
    EXPECT_TRUE(gaps.empty()) << "missing: " << join(gaps);
}
