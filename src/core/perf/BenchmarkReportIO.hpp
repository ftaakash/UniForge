#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "core/common/Result.hpp"
#include "core/common/SvgPlot.hpp"
#include "core/perf/Benchmark.hpp"

namespace uniforge::perf {

// Provenance written next to the measured numbers.
struct ReportProvenance {
    std::string tool;         // "uniforge_cli bench" or "UniForge Performance view"
    std::string toolVersion;
    std::string gitCommit;
    std::string generatedUtc; // ISO 8601
    std::string command;      // command line that regenerates the report
    std::string status;       // e.g. "PROVISIONAL: machine under load"; empty = none
};

// bench_<date>.json: system, workload, per-p rows, agreement, notes.
[[nodiscard]] std::string benchmarkJson(const BenchmarkReport& r, const ReportProvenance& prov);

// bench_<date>.csv: one row per p (warm-up, 5 reps, median/min/max, speedup, efficiency).
// Built with CsvWriter, so it also validates.
[[nodiscard]] Result<std::string> benchmarkCsv(const BenchmarkReport& r);

// bench_<date>.md: human-readable report with the same numbers and the method notes.
[[nodiscard]] std::string benchmarkMarkdown(const BenchmarkReport& r, const ReportProvenance& prov);

// SVG plots of the report, as (file suffix, spec): "_timing.svg" (median, min, max and ideal
// T1/p), "_speedup.svg" (measured vs ideal = p) and "_efficiency.svg".
[[nodiscard]] std::vector<std::pair<std::string, PlotSpec>> benchmarkPlots(
    const BenchmarkReport& r);

// Writes <dir>/<stem>.{json,csv,md}, the three <stem>_*.svg plots and <stem>.meta.json
// (spec §9 sidecar), each atomically.
Result<void> saveBenchmarkReport(const BenchmarkReport& r, const ReportProvenance& prov,
                                 const std::filesystem::path& dir, const std::string& stem);

} // namespace uniforge::perf
