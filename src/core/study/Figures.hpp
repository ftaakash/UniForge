#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/common/Result.hpp"

namespace uniforge::study {

// The report figures P1–P22 (build spec §12.1) from the collected results (results/v2/csv) and
// from small in-process solves for the renders (fields, meshes, patterns, aggregates). Every
// figure is an SVG; renders are embedded PNGs with vector labels on top. The figure tokens of
// docs/report_v2/figure_tokens.md fix colours, line styles and markers per solver.
struct FigureOptions {
    std::filesystem::path results = "results/v2";     // csv/ and history/ are read from here
    std::filesystem::path out = "docs/report_v2/fig"; // P*.svg and figures.md are written here
    std::vector<std::string> only;                    // e.g. {"P4", "P11"}; empty = all
};

struct FigureEntry {
    std::string id;      // "P11"
    std::string file;    // "P11_time_vs_dof.svg"
    std::string title;   // what is shown
    std::string sources; // data files / run ids it is drawn from
};

// Writes every requested figure and figures.md (an index: figure → file → sources). Stops at the
// first figure that fails, with an error naming it.
[[nodiscard]] Result<std::vector<FigureEntry>> makeFigures(const FigureOptions& options);

} // namespace uniforge::study
