#include "app/ui/widgets/NavRail.hpp"

#include <imgui.h>

#include "app/ui/Fonts.hpp"
#include "app/ui/Theme.hpp"

namespace uniforge::ui {

namespace {

constexpr NavItem kItems[kViewCount] = {
    {ViewId::Overview, "Overview", "A", "Task A · workbench overview and self-checks"},
    {ViewId::Geometry, "Geometry", "B", "Task B · STL and nodal-temperature CSV viewer"},
    {ViewId::Thermal, "Thermal", "C", "Task C · 1D steady thermal FEA"},
    {ViewId::Performance, "Performance", "D", "Task D · serial vs parallel batch benchmark"},
    {ViewId::Hardware, "Hardware", "E", "Task E · simulated MCU, FPGA and JTAG adapters"},
    {ViewId::Backprop, "Backprop", "F", "Task F · one-neuron backpropagation check"},
    {ViewId::About, "About", "i", "Version, render backend, launch timing, licences"},
};

constexpr ImGuiKey kDigitKeys[kViewCount] = {ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4,
                                             ImGuiKey_5, ImGuiKey_6, ImGuiKey_7};

} // namespace

ViewId drawNavRail(ViewId current, bool compact, float width, float height) {
    const Theme& t = theme();
    ViewId next = current;

    for (int i = 0; i < kViewCount; ++i) {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | kDigitKeys[i], ImGuiInputFlags_RouteGlobal)) {
            next = kItems[i].id;
        }
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, t.color.bgSurface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(t.size.space12, t.size.space16));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::BeginChild("##nav", ImVec2(width, height), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);

    {
        ScopedFont font(fonts().uiSemiBold, t.type.title);
        ImGui::TextUnformatted(compact ? "UF" : "UniForge");
    }
    if (!compact) {
        ImGui::PushStyleColor(ImGuiCol_Text, t.color.textSecondary);
        ImGui::TextUnformatted("Engineering Workbench");
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0.0f, t.size.space16));

    const float itemH = t.size.controlHeight + t.size.space4;
    for (int i = 0; i < kViewCount; ++i) {
        const NavItem& item = kItems[i];
        if (item.id == ViewId::About) { // About sits apart at the bottom of the list
            ImGui::Dummy(ImVec2(0.0f, t.size.space8));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, t.size.space4));
        }
        const bool selected = item.id == next;
        ImGui::PushID(i);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##item", selected, ImGuiSelectableFlags_None, ImVec2(0.0f, itemH))) {
            next = item.id;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s  (Ctrl+%d)", item.hint, i + 1);
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float textY = pos.y + (itemH - ImGui::GetTextLineHeight()) * 0.5f;
        const float right = pos.x + ImGui::GetContentRegionAvail().x;
        if (selected) { // accent bar on the left edge
            dl->AddRectFilled(ImVec2(pos.x, pos.y + t.size.space4),
                              ImVec2(pos.x + 3.0f * t.dpiScale, pos.y + itemH - t.size.space4),
                              ImGui::GetColorU32(t.color.accent), 2.0f);
        }
        const ImU32 textCol =
            ImGui::GetColorU32(selected ? t.color.textPrimary : t.color.textSecondary);
        const ImU32 tagCol = ImGui::GetColorU32(selected ? t.color.accent : t.color.textDisabled);
        if (compact) {
            const float w = ImGui::CalcTextSize(item.shortTag).x;
            dl->AddText(ImVec2(pos.x + (right - pos.x - w) * 0.5f, textY), textCol, item.shortTag);
        } else {
            dl->AddText(ImVec2(pos.x + t.size.space12, textY), textCol, item.label);
            const float w = ImGui::CalcTextSize(item.shortTag).x;
            dl->AddText(ImVec2(right - w - t.size.space8, textY), tagCol, item.shortTag);
        }
        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    return next;
}

} // namespace uniforge::ui
