#pragma once

#include <string>
#include <vector>

#include <imgui.h>

namespace uniforge::ui {

// The three faces UniForge uses (spec §8.2). Sizes come from TypeScale and are passed per use:
// since Dear ImGui 1.92 a single ImFont renders at any size.
struct FontSet {
    ImFont* ui = nullptr;         // Inter Regular + JetBrains Mono fallback for symbols
    ImFont* uiSemiBold = nullptr; // Inter SemiBold + the same fallback (titles, headings)
    ImFont* mono = nullptr;       // JetBrains Mono Regular (log, tables, IDs)
};

// Loads the fonts embedded in UniForge.exe (RCDATA). On failure returns false and fills
// `error`; ImGui's built-in font is then used so the app still starts.
bool loadFonts(FontSet& fonts, std::string& error);

// Codepoints from GlyphRanges.hpp that the loaded fonts cannot draw, as "U+XXXX" strings.
// Logged at start-up; empty in a correct build (also covered by FontCoverageTest).
[[nodiscard]] std::vector<std::string> missingGlyphs(const FontSet& fonts);

// Glyphs are rasterised on demand at the current DPI. After a DPI change, drop glyphs baked
// at the old size so the atlas is rebuilt at the new one instead of growing.
void onDpiChanged();

const FontSet& fonts();

// RAII helper: pushes a font at a TypeScale size for one scope.
class ScopedFont {
public:
    ScopedFont(ImFont* font, float size) { ImGui::PushFont(font, size); }
    ~ScopedFont() { ImGui::PopFont(); }
    ScopedFont(const ScopedFont&) = delete;
    ScopedFont& operator=(const ScopedFont&) = delete;
};

} // namespace uniforge::ui
