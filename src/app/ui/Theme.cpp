#include "app/ui/Theme.hpp"

#include <cstdint>
#include <initializer_list>

#include <implot.h>

namespace uniforge::ui {

namespace {

constexpr ImVec4 hex(std::uint32_t rgb) {
    return ImVec4(static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
                  static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                  static_cast<float>(rgb & 0xFF) / 255.0f, 1.0f);
}

// Spec §8.2 token table. Contrast is re-checked in Phase 8.
constexpr Palette kLight{
    hex(0xF5F7FA), hex(0xFFFFFF), hex(0xEEF1F5), // bg.app / surface / surface-alt
    hex(0xD3D9E1), hex(0x2563EB),                // border.default / focus
    hex(0x1A1E24), hex(0x59616E), hex(0x9AA1AC), // text.primary / secondary / disabled
    hex(0x2563EB), hex(0x1D4ED8), hex(0x1E40AF), // accent / hover / active
    hex(0x15803D), hex(0xB45309), hex(0xB91C1C), // success / warning / error
    hex(0x0E7490), hex(0x7C3AED),                // info / simulated
};

constexpr Palette kDark{
    hex(0x111317), hex(0x181B21), hex(0x20242C), //
    hex(0x323844), hex(0x5B95FF),                //
    hex(0xE7EAF0), hex(0x9AA3B2), hex(0x5E6573), //
    hex(0x5B95FF), hex(0x7AA9FF), hex(0x3D7BF0), //
    hex(0x3FB950), hex(0xD29922), hex(0xF85149), //
    hex(0x39C5CF), hex(0xA78BFA),                //
};

Theme g_theme;

Metrics scaled(const Metrics& m, float s) {
    Metrics r = m;
    for (float* v :
         {&r.space4, &r.space8, &r.space12, &r.space16, &r.space24, &r.space32, &r.controlHeight,
          &r.cardPadding, &r.radiusControl, &r.radiusCard, &r.navWidth, &r.navCompactWidth,
          &r.compactBreakpoint, &r.statusBarHeight, &r.logPanelHeight}) {
        *v *= s;
    }
    return r;
}

void applyImGuiColors(const Palette& p, ImVec4* c) {
    c[ImGuiCol_Text] = p.textPrimary;
    c[ImGuiCol_TextDisabled] = p.textDisabled;
    c[ImGuiCol_WindowBg] = p.bgApp;
    c[ImGuiCol_ChildBg] = withAlpha(p.bgSurface, 0.0f); // children opt in to a surface fill
    c[ImGuiCol_PopupBg] = p.bgSurface;
    c[ImGuiCol_Border] = p.borderDefault;
    c[ImGuiCol_BorderShadow] = withAlpha(p.bgApp, 0.0f);
    c[ImGuiCol_FrameBg] = p.bgSurfaceAlt;
    c[ImGuiCol_FrameBgHovered] = withAlpha(p.accent, 0.10f);
    c[ImGuiCol_FrameBgActive] = withAlpha(p.accent, 0.18f);
    c[ImGuiCol_TitleBg] = p.bgSurfaceAlt;
    c[ImGuiCol_TitleBgActive] = p.bgSurfaceAlt;
    c[ImGuiCol_TitleBgCollapsed] = p.bgSurfaceAlt;
    c[ImGuiCol_MenuBarBg] = p.bgSurface;
    c[ImGuiCol_ScrollbarBg] = withAlpha(p.bgApp, 0.0f);
    c[ImGuiCol_ScrollbarGrab] = p.borderDefault;
    c[ImGuiCol_ScrollbarGrabHovered] = p.textDisabled;
    c[ImGuiCol_ScrollbarGrabActive] = p.textSecondary;
    c[ImGuiCol_CheckMark] = p.accent;
    c[ImGuiCol_CheckboxSelectedBg] = withAlpha(p.accent, 0.14f);
    c[ImGuiCol_SliderGrab] = p.accent;
    c[ImGuiCol_SliderGrabActive] = p.accentActive;
    c[ImGuiCol_Button] = p.bgSurfaceAlt;
    c[ImGuiCol_ButtonHovered] = withAlpha(p.accent, 0.14f);
    c[ImGuiCol_ButtonActive] = withAlpha(p.accent, 0.24f);
    c[ImGuiCol_Header] = withAlpha(p.accent, 0.14f);
    c[ImGuiCol_HeaderHovered] = withAlpha(p.accent, 0.08f);
    c[ImGuiCol_HeaderActive] = withAlpha(p.accent, 0.20f);
    c[ImGuiCol_Separator] = p.borderDefault;
    c[ImGuiCol_SeparatorHovered] = p.borderFocus;
    c[ImGuiCol_SeparatorActive] = p.borderFocus;
    c[ImGuiCol_ResizeGrip] = withAlpha(p.borderDefault, 0.0f);
    c[ImGuiCol_ResizeGripHovered] = p.borderFocus;
    c[ImGuiCol_ResizeGripActive] = p.borderFocus;
    c[ImGuiCol_InputTextCursor] = p.textPrimary;
    c[ImGuiCol_Tab] = p.bgSurfaceAlt;
    c[ImGuiCol_TabHovered] = withAlpha(p.accent, 0.14f);
    c[ImGuiCol_TabSelected] = p.bgSurface;
    c[ImGuiCol_TabSelectedOverline] = p.accent;
    c[ImGuiCol_TabDimmed] = p.bgSurfaceAlt;
    c[ImGuiCol_TabDimmedSelected] = p.bgSurface;
    c[ImGuiCol_TabDimmedSelectedOverline] = p.borderDefault;
    c[ImGuiCol_DockingPreview] = withAlpha(p.accent, 0.4f);
    c[ImGuiCol_DockingEmptyBg] = p.bgApp;
    c[ImGuiCol_PlotLines] = p.accent;
    c[ImGuiCol_PlotLinesHovered] = p.accentHover;
    c[ImGuiCol_PlotHistogram] = p.accent; // progress bars
    c[ImGuiCol_PlotHistogramHovered] = p.accentHover;
    c[ImGuiCol_TableHeaderBg] = p.bgSurfaceAlt;
    c[ImGuiCol_TableBorderStrong] = p.borderDefault;
    c[ImGuiCol_TableBorderLight] = p.borderDefault;
    c[ImGuiCol_TableRowBg] = withAlpha(p.bgSurface, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = withAlpha(p.bgSurfaceAlt, 0.6f);
    c[ImGuiCol_TextLink] = p.accent;
    c[ImGuiCol_TextSelectedBg] = withAlpha(p.accent, 0.25f);
    c[ImGuiCol_TreeLines] = p.borderDefault;
    c[ImGuiCol_DragDropTarget] = p.borderFocus;
    c[ImGuiCol_DragDropTargetBg] = withAlpha(p.accent, 0.0f);
    c[ImGuiCol_UnsavedMarker] = p.textPrimary;
    c[ImGuiCol_NavCursor] = p.borderFocus;
    c[ImGuiCol_NavWindowingHighlight] = p.borderFocus;
    c[ImGuiCol_NavWindowingDimBg] = withAlpha(p.textPrimary, 0.2f);
    c[ImGuiCol_ModalWindowDimBg] = withAlpha(p.textPrimary, 0.35f);
}

void applyImPlotStyle(const Palette& p, const Metrics& m) {
    ImPlotStyle& s = ImPlot::GetStyle();
    s = ImPlotStyle();
    s.PlotPadding = ImVec2(m.space12, m.space12);
    s.LabelPadding = ImVec2(m.space4, m.space4);
    s.LegendPadding = ImVec2(m.space8, m.space8);
    ImVec4* c = s.Colors;
    c[ImPlotCol_FrameBg] = withAlpha(p.bgSurface, 0.0f);
    c[ImPlotCol_PlotBg] = p.bgSurface;
    c[ImPlotCol_PlotBorder] = p.borderDefault;
    c[ImPlotCol_LegendBg] = p.bgSurface;
    c[ImPlotCol_LegendBorder] = p.borderDefault;
    c[ImPlotCol_LegendText] = p.textPrimary;
    c[ImPlotCol_TitleText] = p.textPrimary;
    c[ImPlotCol_InlayText] = p.textSecondary;
    c[ImPlotCol_AxisText] = p.textSecondary;
    c[ImPlotCol_AxisGrid] = withAlpha(p.borderDefault, 0.7f);
    c[ImPlotCol_AxisBg] = withAlpha(p.bgSurface, 0.0f);
    c[ImPlotCol_Selection] = p.accent;
    c[ImPlotCol_Crosshairs] = p.textSecondary;
}

} // namespace

const Theme& theme() {
    return g_theme;
}

ImVec4 withAlpha(const ImVec4& c, float alpha) {
    return ImVec4(c.x, c.y, c.z, alpha);
}

void applyTheme(ThemeMode mode, float dpiScale) {
    g_theme.mode = mode;
    g_theme.dpiScale = dpiScale;
    g_theme.color = (mode == ThemeMode::Dark) ? kDark : kLight;
    g_theme.size = scaled(Metrics{}, dpiScale);

    // Rebuild from defaults at 100 %, then scale once: ScaleAllSizes() is multiplicative.
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    const Metrics base{};
    const TypeScale type{};
    style.FontSizeBase = type.body;
    style.WindowPadding = ImVec2(base.space16, base.space16);
    style.FramePadding = ImVec2(base.space12, (base.controlHeight - type.body) * 0.5f);
    style.ItemSpacing = ImVec2(base.space8, base.space8);
    style.ItemInnerSpacing = ImVec2(base.space8, base.space4);
    style.CellPadding = ImVec2(base.space8, base.space4);
    style.IndentSpacing = base.space16;
    style.ScrollbarSize = base.space12;
    style.GrabMinSize = base.space12;
    style.WindowRounding = 0.0f;
    style.ChildRounding = base.radiusCard;
    style.FrameRounding = base.radiusControl;
    style.PopupRounding = base.radiusControl;
    style.GrabRounding = base.radiusControl;
    style.TabRounding = base.radiusControl;
    style.ScrollbarRounding = base.radiusControl;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.SeparatorTextBorderSize = 1.0f;
    style.ScaleAllSizes(dpiScale);
    style.FontScaleDpi = dpiScale;
    applyImGuiColors(g_theme.color, style.Colors);

    applyImPlotStyle(g_theme.color, g_theme.size);
}

const ImVec4& statusColor(Status status) {
    const Palette& p = g_theme.color;
    switch (status) {
    case Status::Success:
        return p.success;
    case Status::Warning:
        return p.warning;
    case Status::Error:
        return p.error;
    case Status::Info:
        return p.info;
    case Status::Simulated:
        return p.simulated;
    case Status::Pending:
        return p.textSecondary;
    }
    return p.textSecondary;
}

const char* statusGlyph(Status status) {
    switch (status) {
    case Status::Success:
        return "✓";
    case Status::Warning:
        return "!";
    case Status::Error:
        return "✕";
    case Status::Info:
        return "i";
    case Status::Simulated:
        return "◇";
    case Status::Pending:
        return "·";
    }
    return "·";
}

} // namespace uniforge::ui
