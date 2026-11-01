#include "app/ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cstdio>
#include <numeric>

#include "app/platform/FileDialog.hpp"
#include "app/platform/Text.hpp"
#include "app/ui/Fonts.hpp"
#include "app/ui/Format.hpp"
#include "app/ui/ThemeViz.hpp"
#include "app/ui/widgets/Widgets.hpp"

namespace uniforge::ui {

namespace {

const char* passWord(Status s) {
    return s == Status::Success ? "pass" : (s == Status::Error ? "fail" : "");
}

} // namespace

void dataTable(const char* id, const std::vector<TableColumn>& columns,
               const std::vector<TableRow>& rows, bool passColumn) {
    const Theme& t = theme();
    const int colCount = static_cast<int>(columns.size()) + (passColumn ? 1 : 0);
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable(id, colCount, flags)) {
        return;
    }
    for (const auto& c : columns) {
        ImGui::TableSetupColumn(c.header.c_str());
    }
    if (passColumn) {
        ImGui::TableSetupColumn("pass", ImGuiTableColumnFlags_WidthFixed);
    }
    ImGui::TableHeadersRow();

    // Shown order: sorted by the clicked column, otherwise as given.
    std::vector<std::size_t> order(rows.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    if (ImGuiTableSortSpecs* sort = ImGui::TableGetSortSpecs(); sort && sort->SpecsCount > 0) {
        const auto& spec = sort->Specs[0];
        const auto col = static_cast<std::size_t>(spec.ColumnIndex);
        const bool asc = spec.SortDirection != ImGuiSortDirection_Descending;
        if (col < columns.size()) {
            std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                const auto& ca = rows[a].cells[col];
                const auto& cb = rows[b].cells[col];
                const bool less =
                    columns[col].numeric ? ca.sortKey < cb.sortKey : ca.text < cb.text;
                const bool greater =
                    columns[col].numeric ? cb.sortKey < ca.sortKey : cb.text < ca.text;
                return asc ? less : greater;
            });
        }
    }

    for (const std::size_t r : order) {
        ImGui::TableNextRow();
        for (std::size_t c = 0; c < columns.size(); ++c) {
            ImGui::TableNextColumn();
            const std::string& text =
                c < rows[r].cells.size() ? rows[r].cells[c].text : std::string();
            if (columns[c].numeric) {
                ScopedFont mono(fonts().mono, t.type.mono);
                const float w = ImGui::CalcTextSize(text.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w);
                ImGui::TextUnformatted(text.c_str());
            } else {
                ImGui::TextUnformatted(text.c_str());
            }
        }
        if (passColumn) {
            ImGui::TableNextColumn();
            if (rows[r].status != Status::Pending) {
                statusLabel(rows[r].status, passWord(rows[r].status));
            }
        }
    }
    ImGui::EndTable();

    if (ImGui::IsItemHovered() && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
        std::string tsv;
        for (std::size_t c = 0; c < columns.size(); ++c) {
            tsv += (c ? "\t" : "") + columns[c].header;
        }
        tsv += passColumn ? "\tpass\n" : "\n";
        for (const std::size_t r : order) {
            for (std::size_t c = 0; c < rows[r].cells.size(); ++c) {
                tsv += (c ? "\t" : "") + rows[r].cells[c].text;
            }
            tsv += passColumn ? std::string("\t") + passWord(rows[r].status) + "\n" : "\n";
        }
        ImGui::SetClipboardText(tsv.c_str());
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("Click a header to sort · Ctrl+C copies the table as TSV");
    }
}

int exportMenu(const char* id, const std::vector<std::string>& items, bool enabled,
               const char* disabledReason) {
    int chosen = -1;
    ImGui::PushID(id);
    if (secondaryButton("Export ▾", enabled, disabledReason)) {
        ImGui::OpenPopup("##export");
    }
    if (ImGui::BeginPopup("##export")) {
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (ImGui::MenuItem(items[i].c_str())) {
                chosen = static_cast<int>(i);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return chosen;
}

void exportStatusLine(const ExportStatus& s) {
    if (s.file.empty() && s.detail.empty()) {
        return;
    }
    if (!s.ok) {
        statusLabel(Status::Error, "Export failed", s.detail.c_str());
        return;
    }
    const std::string text =
        platform::pathToUtf8(s.file.filename()) + (s.detail.empty() ? "" : " " + s.detail);
    statusLabel(Status::Success, "Saved", text.c_str());
    ImGui::SameLine(0.0f, theme().size.space8);
    if (ImGui::TextLink("Open folder")) {
        platform::revealInExplorer(s.file);
    }
}

ScreenRect lastItemRect() {
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    return {a.x, a.y, b.x - a.x, b.y - a.y};
}

ImVec2 colorLegend(ImDrawList* dl, ImVec2 topLeft, float height, double minC, double maxC,
                   const char* title) {
    const Theme& t = theme();
    ImGui::PushFont(fonts().ui, t.type.label);
    const float barW = 14.0f * t.dpiScale;
    const float pad = t.size.space8;
    const float titleH = ImGui::GetTextLineHeight() + t.size.space4;

    char labels[5][32];
    float labelW = 0.0f;
    for (int i = 0; i < 5; ++i) {
        const double v = maxC - (maxC - minC) * i / 4.0;
        std::snprintf(labels[i], sizeof labels[i], "%.2f", v);
        labelW = std::max(labelW, ImGui::CalcTextSize(labels[i]).x);
    }
    const float width =
        std::max(barW + t.size.space8 + labelW, ImGui::CalcTextSize(title).x) + pad * 2;
    const ImVec2 size(width, height);
    dl->AddRectFilled(topLeft, ImVec2(topLeft.x + size.x, topLeft.y + size.y),
                      ImGui::GetColorU32(withAlpha(t.color.bgSurface, 0.88f)),
                      t.size.radiusControl);
    dl->AddRect(topLeft, ImVec2(topLeft.x + size.x, topLeft.y + size.y),
                ImGui::GetColorU32(t.color.borderDefault), t.size.radiusControl);
    dl->AddText(ImVec2(topLeft.x + pad, topLeft.y + pad), ImGui::GetColorU32(t.color.textPrimary),
                title);

    const float barTop = topLeft.y + pad + titleH + ImGui::GetTextLineHeight() * 0.5f;
    const float barBottom = topLeft.y + height - pad - ImGui::GetTextLineHeight() * 0.5f;
    const float x0 = topLeft.x + pad;
    constexpr int kSteps = 32;
    for (int i = 0; i < kSteps; ++i) {
        const float y0 = barTop + (barBottom - barTop) * i / kSteps;
        const float y1 = barTop + (barBottom - barTop) * (i + 1) / kSteps;
        const ImU32 top = ImGui::GetColorU32(colormapColor(1.0f - static_cast<float>(i) / kSteps));
        const ImU32 bottom =
            ImGui::GetColorU32(colormapColor(1.0f - static_cast<float>(i + 1) / kSteps));
        dl->AddRectFilledMultiColor(ImVec2(x0, y0), ImVec2(x0 + barW, y1 + 0.5f), top, top, bottom,
                                    bottom);
    }
    dl->AddRect(ImVec2(x0, barTop), ImVec2(x0 + barW, barBottom),
                ImGui::GetColorU32(t.color.borderDefault));
    for (int i = 0; i < 5; ++i) {
        const float y = barTop + (barBottom - barTop) * i / 4.0f;
        dl->AddLine(ImVec2(x0 + barW, y), ImVec2(x0 + barW + t.size.space4, y),
                    ImGui::GetColorU32(t.color.textSecondary));
        dl->AddText(ImVec2(x0 + barW + t.size.space8, y - ImGui::GetTextLineHeight() * 0.5f),
                    ImGui::GetColorU32(t.color.textPrimary), labels[i]);
    }
    ImGui::PopFont();
    return size;
}

void nodeInspector(const InspectedNode* node) {
    const Theme& t = theme();
    if (!node) {
        secondaryText("Click a point in the viewport to inspect it (nearest node within 6 px).");
        return;
    }
    if (ImGui::BeginTable("##node", 2, ImGuiTableFlags_SizingStretchProp)) {
        auto row = [&](const char* name, const std::string& value) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, t.color.textSecondary);
            ImGui::TextUnformatted(name);
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ScopedFont mono(fonts().mono, t.type.mono);
            const float w = ImGui::CalcTextSize(value.c_str()).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w);
            ImGui::TextUnformatted(value.c_str());
        };
        row("node_id", std::to_string(node->id));
        row("x", formatCoordinate(node->x));
        row("y", formatCoordinate(node->y));
        row("z", formatCoordinate(node->z));
        row("T", formatTemperature(node->T_C));
        ImGui::EndTable();
    }
}

} // namespace uniforge::ui
