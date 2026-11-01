#pragma once

#include <filesystem>
#include <string>

#include "core/common/Result.hpp"
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

// Writes <dir>/<stem>.{json,csv,md} and <stem>.meta.json (spec §9 sidecar), each atomically.
Result<void> saveBenchmarkReport(const BenchmarkReport& r, const ReportProvenance& prov,
                                 const std::filesystem::path& dir, const std::string& stem);

} // namespace uniforge::perf
