#include "app/ui/widgets/Widgets.hpp"

#include <cstdarg>

#include "app/ui/Fonts.hpp"

namespace uniforge::ui {

namespace {

void disabledTooltip(const char* reason) {
    if (reason && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", reason);
    }
}

bool button(const char* label, bool enabled, const char* disabledReason, bool primary) {
    const Theme& t = theme();
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, t.color.accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.color.accentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.color.accentActive);
        ImGui::PushStyleColor(ImGuiCol_Text, t.color.bgSurface);
    }
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::Button(label);
    ImGui::EndDisabled();
    if (primary) {
        ImGui::PopStyleColor(4);
    }
    if (!enabled) {
        disabledTooltip(disabledReason);
    }
    return pressed && enabled;
}

} // namespace

void viewHeader(const char* title, const char* tag, const char* purpose) {
    const Theme& t = theme();
    {
        ScopedFont font(fonts().uiSemiBold, t.type.title);
        ImGui::TextUnformatted(title);
    }
    if (tag && tag[0] != '\0') {
        ImGui::SameLine(0.0f, t.size.space12);
        chip(tag, t.color.accent);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, t.color.textSecondary);
    ImGui::TextWrapped("%s", purpose);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, t.size.space8));
}

void beginCard(const char* id, float width) {
    const Theme& t = theme();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, t.color.bgSurface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(t.size.cardPadding, t.size.cardPadding));
    ImGui::BeginChild(id, ImVec2(width, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                          ImGuiChildFlags_AlwaysUseWindowPadding);
}

void endCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void cardTitle(const char* text) {
    ScopedFont font(fonts().uiSemiBold, theme().type.heading);
    ImGui::TextUnformatted(text);
}

void emptyState(const char* title, const char* body) {
    const Theme& t = theme();
    const float wrap = 460.0f * t.dpiScale;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float width = avail < wrap ? avail : wrap;
    const float indent = (avail - width) * 0.5f;

    ImGui::Dummy(ImVec2(0.0f, t.size.space32));
    {
        ScopedFont font(fonts().uiSemiBold, t.type.heading);
        const float titleW = ImGui::CalcTextSize(title).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - titleW) * 0.5f);
        ImGui::TextUnformatted(title);
    }
    ImGui::Dummy(ImVec2(0.0f, t.size.space4));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::PushStyleColor(ImGuiCol_Text, t.color.textSecondary);
    ImGui::TextWrapped("%s", body);
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0.0f, t.size.space32));
}

void chip(const char* text, const ImVec4& color) {
    const Theme& t = theme();
    ScopedFont font(fonts().ui, t.type.label);
    const ImVec2 pad(t.size.space8, t.size.space4 * 0.5f);
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 size(textSize.x + pad.x * 2.0f, textSize.y + pad.y * 2.0f);
    // Centre the chip on the current line.
    const float lineH = ImGui::GetTextLineHeightWithSpacing();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                      ImGui::GetColorU32(withAlpha(color, 0.12f)), t.size.radiusControl);
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                ImGui::GetColorU32(withAlpha(color, 0.45f)), t.size.radiusControl);
    dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), ImGui::GetColorU32(color), text);
    ImGui::Dummy(ImVec2(size.x, size.y > lineH ? size.y : lineH));
}

void statusLabel(Status status, const char* word, const char* detail) {
    const Theme& t = theme();
    ImGui::PushStyleColor(ImGuiCol_Text, statusColor(status));
    ImGui::Text("%s %s", statusGlyph(status), word);
    ImGui::PopStyleColor();
    if (detail && detail[0] != '\0') {
        ImGui::SameLine(0.0f, t.size.space8);
        ImGui::PushStyleColor(ImGuiCol_Text, t.color.textSecondary);
        ImGui::TextUnformatted(detail);
        ImGui::PopStyleColor();
    }
}

bool primaryButton(const char* label, bool enabled, const char* disabledReason) {
    return button(label, enabled, disabledReason, true);
}

bool secondaryButton(const char* label, bool enabled, const char* disabledReason) {
    return button(label, enabled, disabledReason, false);
}

void secondaryText(const char* fmt, ...) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme().color.textSecondary);
    va_list args;
    va_start(args, fmt);
    ImGui::TextWrappedV(fmt, args);
    va_end(args);
    ImGui::PopStyleColor();
}

} // namespace uniforge::ui
