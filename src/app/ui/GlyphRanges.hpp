#pragma once

// Unicode coverage UniForge needs from its embedded fonts (spec §8.2). Kept free of ImGui so the
// font-coverage unit test can check the same table against the TTF files directly.
//
// Since Dear ImGui 1.92 glyphs are rasterised on demand, so these ranges are not a load filter;
// they are the contract that Fonts::missingGlyphs() and tests/unit/ui/FontCoverageTest check.

namespace uniforge::glyphs {

// Inclusive [first, last] pairs, zero-terminated (ImGui's ImWchar range layout).
inline constexpr unsigned int kUiRanges[] = {
    0x0020, 0x007E, // Basic Latin
    0x00A0, 0x00FF, // Latin-1 Supplement: ° ² ³ · × ± µ
    0x0391, 0x03A1, // Greek capitals Α–Ρ (U+03A2 is unassigned)
    0x03A3, 0x03A9, // Greek capitals Σ–Ω
    0x03B1, 0x03C9, // Greek small α–ω (ε, Δ used in tolerances and differences)
    0x2014, 0x2014, // — em dash (error copy)
    0x2026, 0x2026, // … ellipsis
    0x2192, 0x2192, // → arrow ("Open view →")
    0x2212, 0x2212, // − minus sign
    0x2264, 0x2265, // ≤ ≥ (tolerances)
    0x25C7, 0x25C7, // ◇ simulated
    0x25CF, 0x25CF, // ● status dot
    0x2713, 0x2713, // ✓ pass
    0x2715, 0x2715, // ✕ fail
    0,
};

// The mono font is used for the log, tables and IDs: ASCII plus the symbols that appear there.
inline constexpr unsigned int kMonoRanges[] = {
    0x0020, 0x007E, // Basic Latin
    0x00B0, 0x00B0, // °
    0x00B7, 0x00B7, // ·
    0x00D7, 0x00D7, // ×
    0x2014, 0x2014, // —
    0x2212, 0x2212, // −
    0x2264, 0x2265, // ≤ ≥
    0,
};

} // namespace uniforge::glyphs
