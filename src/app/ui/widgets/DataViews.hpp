#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <imgui.h>

#include "app/ui/Theme.hpp"

// Result presentation widgets (spec §8.4): DataTable, ExportMenu, PlotPanel export,
// ColorLegend, NodeInspector.
namespace uniforge::ui {

// ---- DataTable ------------------------------------------------------------------------------
struct TableColumn {
    std::string header;  // "max mid err [°C]"
    bool numeric = true; // mono, right-aligned, sorted by value
};

struct TableCell {
    std::string text;
    double sortKey = 0.0; // used when the column is numeric
};

struct TableRow {
    std::vector<TableCell> cells;
    Status status = Status::Pending; // pass column: Success/Error; Pending = no pass column cell
};

// Sortable table with a final pass column when `passColumn` is set. Ctrl+C while hovered
// copies the table as TSV (header + rows in the shown order).
void dataTable(const char* id, const std::vector<TableColumn>& columns,
               const std::vector<TableRow>& rows, bool passColumn);

// ---- ExportMenu -----------------------------------------------------------------------------
// "Export ▾" button with one entry per item; returns the chosen index or -1.
// Disabled with `disabledReason` when `enabled` is false.
int exportMenu(const char* id, const std::vector<std::string>& items, bool enabled,
               const char* disabledReason);

// After an export: "✓ Saved convergence.csv (4 rows) · Open folder" / "✕ <message>".
struct ExportStatus {
    bool ok = false;
    std::filesystem::path file;
    std::string detail; // "(4 rows)" or the error message + hint
};
void exportStatusLine(const ExportStatus& status);

// ---- PlotPanel ------------------------------------------------------------------------------
// Screen rectangle of the last item (the plot), for PNG export via platform::requestCapture.
struct ScreenRect {
    float x = 0, y = 0, w = 0, h = 0;
};
[[nodiscard]] ScreenRect lastItemRect();

// ---- ColorLegend ----------------------------------------------------------------------------
// Vertical inferno bar from max (top) to min (bottom) with 5 ticks in °C, drawn into `dl`
// at `topLeft` (used as an overlay on the viewport's right edge). Returns its size.
ImVec2 colorLegend(ImDrawList* dl, ImVec2 topLeft, float height, double minC, double maxC,
                   const char* title);

// ---- NodeInspector --------------------------------------------------------------------------
struct InspectedNode {
    long long id = 0;
    double x = 0, y = 0, z = 0; // m, file coordinates
    double T_C = 0;
};
// Card body: id, x/y/z [m], T [°C], or a hint when nothing is picked.
void nodeInspector(const InspectedNode* node);

} // namespace uniforge::ui
