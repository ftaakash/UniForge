#pragma once

#include <imgui.h>

namespace uniforge::ui {

enum class ThemeMode { Light, Dark };

// Colour tokens from spec §8.2. Views and widgets read colours only from here; literal colours
// outside Theme.cpp fail the CI lint (spec §4.2).
struct Palette {
    ImVec4 bgApp, bgSurface, bgSurfaceAlt;
    ImVec4 borderDefault, borderFocus;
    ImVec4 textPrimary, textSecondary, textDisabled;
    ImVec4 accent, accentHover, accentActive;
    ImVec4 success, warning, error, info, simulated;
};

// Sizes in pixels at 100 % scaling; Theme scales them by the window DPI.
struct Metrics {
    float space4 = 4, space8 = 8, space12 = 12, space16 = 16, space24 = 24, space32 = 32;
    float controlHeight = 30;
    float cardPadding = 16;
    float radiusControl = 4;
    float radiusCard = 6;
    float navWidth = 208;
    float navCompactWidth = 72;
    float compactBreakpoint = 1100; // window width below which the NavRail goes compact
    float statusBarHeight = 30;
    float logPanelHeight = 190;
};

// Font sizes at 100 % (spec §8.2 type tokens). ImGui scales them by style.FontScaleDpi.
struct TypeScale {
    float title = 20, heading = 16, body = 15, label = 13, mono = 14;
};

// Status is never colour-only (spec §8.1): every status has a glyph and a word as well.
enum class Status { Success, Warning, Error, Info, Simulated, Pending };

struct Theme {
    ThemeMode mode = ThemeMode::Light;
    float dpiScale = 1.0f;
    Palette color{};
    Metrics size{}; // already multiplied by dpiScale
    TypeScale type{};
};

// Current theme (valid after applyTheme()).
[[nodiscard]] const Theme& theme();

// Rebuilds ImGui and ImPlot styles from the tokens for `mode` at `dpiScale`.
// Safe to call again on DPI or light/dark changes: the style is rebuilt, not rescaled.
void applyTheme(ThemeMode mode, float dpiScale);

[[nodiscard]] const ImVec4& statusColor(Status status);
[[nodiscard]] const char* statusGlyph(Status status); // ✓ ! ✕ i ◇ ·

// Same colour with a different alpha (hover tints, selection backgrounds).
[[nodiscard]] ImVec4 withAlpha(const ImVec4& c, float alpha);

} // namespace uniforge::ui
