#include "app/ui/Fonts.hpp"

#include <cstdio>

#include "app/platform/Resources.hpp"
#include "app/resource.h"
#include "app/ui/GlyphRanges.hpp"
#include "app/ui/Theme.hpp"

namespace uniforge::ui {

namespace {

FontSet g_fonts;

// ImGui's legacy range type is ImWchar; convert the shared table once.
template <std::size_t N> std::vector<ImWchar> toImWchar(const unsigned int (&ranges)[N]) {
    std::vector<ImWchar> out;
    out.reserve(N);
    for (unsigned int v : ranges) {
        out.push_back(static_cast<ImWchar>(v));
    }
    return out;
}

const std::vector<ImWchar>& uiRanges() {
    static const std::vector<ImWchar> r = toImWchar(glyphs::kUiRanges);
    return r;
}

const std::vector<ImWchar>& monoRanges() {
    static const std::vector<ImWchar> r = toImWchar(glyphs::kMonoRanges);
    return r;
}

ImFont* addEmbedded(int resourceId, float size, const char* name, const ImWchar* ranges,
                    ImFont* mergeInto, std::string& error) {
    const platform::ResourceBytes bytes = platform::loadResource(resourceId);
    if (!bytes.data) {
        error += std::string("embedded font ") + name + " missing; ";
        return nullptr;
    }
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false; // memory belongs to the executable image
    cfg.MergeMode = mergeInto != nullptr;
    cfg.DstFont = mergeInto;
    cfg.GlyphRanges = ranges; // only consulted by legacy renderers; documents the contract
    std::snprintf(cfg.Name, sizeof cfg.Name, "%s", name);
    // ImGui never writes to the font data when FontDataOwnedByAtlas is false.
    void* data = const_cast<void*>(bytes.data);
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data, static_cast<int>(bytes.size), size,
                                                      &cfg);
}

bool covers(ImFont* font, const unsigned int* ranges, std::vector<std::string>& missing) {
    bool ok = true;
    for (; ranges[0] != 0; ranges += 2) {
        for (unsigned int c = ranges[0]; c <= ranges[1]; ++c) {
            if (!font->IsGlyphInFont(static_cast<ImWchar>(c))) {
                char buf[16];
                std::snprintf(buf, sizeof buf, "U+%04X", c);
                missing.emplace_back(buf);
                ok = false;
            }
        }
    }
    return ok;
}

} // namespace

bool loadFonts(FontSet& fonts, std::string& error) {
    const TypeScale type{};
    error.clear();
    // Inter first; JetBrains Mono merged behind it supplies symbols Inter lacks (✓ ✕ ◇ …).
    fonts.ui = addEmbedded(IDR_FONT_INTER_REGULAR, type.body, "Inter Regular", uiRanges().data(),
                           nullptr, error);
    if (fonts.ui) {
        addEmbedded(IDR_FONT_MONO_REGULAR, type.body, "JetBrains Mono (fallback)",
                    uiRanges().data(), fonts.ui, error);
    }
    fonts.uiSemiBold = addEmbedded(IDR_FONT_INTER_SEMIBOLD, type.heading, "Inter SemiBold",
                                   uiRanges().data(), nullptr, error);
    if (fonts.uiSemiBold) {
        addEmbedded(IDR_FONT_MONO_REGULAR, type.heading, "JetBrains Mono (fallback)",
                    uiRanges().data(), fonts.uiSemiBold, error);
    }
    fonts.mono = addEmbedded(IDR_FONT_MONO_REGULAR, type.mono, "JetBrains Mono Regular",
                             monoRanges().data(), nullptr, error);

    ImGuiIO& io = ImGui::GetIO();
    if (!fonts.ui || !fonts.uiSemiBold || !fonts.mono) {
        ImFont* fallback = io.Fonts->AddFontDefault();
        fonts.ui = fonts.ui ? fonts.ui : fallback;
        fonts.uiSemiBold = fonts.uiSemiBold ? fonts.uiSemiBold : fallback;
        fonts.mono = fonts.mono ? fonts.mono : fallback;
    }
    io.FontDefault = fonts.ui;
    g_fonts = fonts;
    return error.empty();
}

std::vector<std::string> missingGlyphs(const FontSet& fonts) {
    std::vector<std::string> missing;
    covers(fonts.ui, glyphs::kUiRanges, missing);
    covers(fonts.uiSemiBold, glyphs::kUiRanges, missing);
    covers(fonts.mono, glyphs::kMonoRanges, missing);
    return missing;
}

void onDpiChanged() {
    ImGui::GetIO().Fonts->CompactCache();
}

const FontSet& fonts() {
    return g_fonts;
}

} // namespace uniforge::ui
