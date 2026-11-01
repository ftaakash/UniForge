#include "core/perf/BenchmarkReportIO.hpp"

#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

#include "core/common/AtomicFile.hpp"
#include "core/common/CsvWriter.hpp"
#include "core/common/MetaJson.hpp"
#include "core/common/SvgPlot.hpp"

namespace uniforge::perf {

namespace {

std::string fmt(const char* format, double v) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, format, v);
    return buffer;
}

std::string jsonString(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char escaped[8];
                std::snprintf(escaped, sizeof escaped, "\\u%04x",
                              static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += escaped;
            } else {
                out += c;
            }
        }
    }
    return out + "\"";
}

std::string jsonNumber(double v) {
    return std::isfinite(v) ? formatFullPrecision(v) : std::string("null");
}

std::string orUnknown(const std::string& s) {
    return s.empty() ? std::string("unknown") : s;
}

std::string intsText(const std::vector<int>& values) {
    std::string s;
    for (const int v : values) {
        s += (s.empty() ? "" : ", ") + std::to_string(v);
    }
    return s;
}

std::string gb(std::uint64_t bytes) {
    return fmt("%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
}

} // namespace

std::string benchmarkJson(const BenchmarkReport& r, const ReportProvenance& prov) {
    std::string j = "{\n";
    const auto field = [&j](int indent, const std::string& key, const std::string& value,
                            bool last = false) {
        j += std::string(static_cast<std::size_t>(indent), ' ') + jsonString(key) + ": " + value +
             (last ? "\n" : ",\n");
    };
    field(2, "tool", jsonString(prov.tool));
    field(2, "tool_version", jsonString(prov.toolVersion));
    field(2, "git_commit", jsonString(prov.gitCommit));
    field(2, "generated_utc", jsonString(prov.generatedUtc));
    field(2, "status", jsonString(prov.status));
    field(2, "regenerate", jsonString(prov.command));

    j += "  \"system\": {\n";
    field(4, "cpu", jsonString(r.sys.cpu));
    field(4, "core_types", jsonString(r.sys.coreTypes));
    field(4, "physical_cores", std::to_string(r.sys.physicalCores));
    field(4, "logical_cpus", std::to_string(r.sys.logicalCpus));
    field(4, "ram_bytes", std::to_string(r.sys.ramBytes));
    field(4, "os", jsonString(r.sys.os));
    field(4, "compiler", jsonString(r.sys.compiler));
    field(4, "flags", jsonString(r.sys.flags));
    field(4, "build_type", jsonString(r.sys.buildType), true);
    j += "  },\n";

    j += "  \"workload\": {\n";
    field(4, "seed", std::to_string(r.cfg.seed));
    field(4, "cases", std::to_string(r.cfg.cases));
    field(4, "elements", std::to_string(r.cfg.elements));
    field(4, "k_range_W_per_mK", "[5, 50]");
    field(4, "q_range_W_per_m3", "[1000, 50000]");
    field(4, "result_set_bytes", jsonNumber(r.resultSetBytes));
    field(4, "requested_workers", "[" + intsText(r.cfg.workers) + "]");
    field(4, "effective_workers", "[" + intsText(r.effectiveWorkers) + "]");
    field(4, "warmup_reps", std::to_string(kWarmupReps));
    field(4, "timed_reps", std::to_string(kTimedReps), true);
    j += "  },\n";

    j += "  \"rows\": [\n";
    for (std::size_t i = 0; i < r.rows.size(); ++i) {
        const BenchRow& row = r.rows[i];
        std::string reps;
        for (const double s : row.repsSec) {
            reps += (reps.empty() ? "" : ", ") + jsonNumber(s);
        }
        j += "    {\"workers\": " + std::to_string(row.workers) +
             ", \"threads\": " + std::to_string(row.threads) +
             ", \"warmup_s\": " + jsonNumber(row.warmupSec) + ", \"reps_s\": [" + reps +
             "], \"median_s\": " + jsonNumber(row.median) + ", \"min_s\": " +
             jsonNumber(row.min) + ", \"max_s\": " + jsonNumber(row.max) +
             ", \"speedup\": " + jsonNumber(row.speedup) +
             ", \"efficiency\": " + jsonNumber(row.efficiency) + "}" +
             (i + 1 < r.rows.size() ? ",\n" : "\n");
    }
    j += "  ],\n";

    j += "  \"agreement\": {\n";
    field(4, "max_abs_diff_C", jsonNumber(r.maxAbsDiff_C));
    field(4, "tolerance_C", jsonNumber(kAgreementTolerance_C));
    field(4, "pass", r.agreementPass ? "true" : "false");
    field(4, "order_preserved", r.orderPreserved ? "true" : "false");
    field(4, "speedup_assessable", r.speedupAssessable ? "true" : "false", true);
    j += "  },\n";

    j += "  \"notes\": [\n";
    for (std::size_t i = 0; i < r.notes.size(); ++i) {
        j += "    " + jsonString(r.notes[i]) + (i + 1 < r.notes.size() ? ",\n" : "\n");
    }
    j += "  ]\n}\n";
    return j;
}

Result<std::string> benchmarkCsv(const BenchmarkReport& r) {
    std::vector<std::string> cols{"workers", "threads", "warmup_s"};
    for (int i = 1; i <= kTimedReps; ++i) {
        cols.push_back("rep" + std::to_string(i) + "_s");
    }
    for (const char* c : {"median_s", "min_s", "max_s", "speedup", "efficiency"}) {
        cols.emplace_back(c);
    }
    CsvWriter csv(cols, CsvNumberFormat::Shortest);
    for (const BenchRow& row : r.rows) {
        csv.beginRow();
        csv.add(row.workers);
        csv.add(row.threads);
        csv.add(row.warmupSec);
        for (const double s : row.repsSec) {
            csv.add(s);
        }
        csv.add(row.median);
        csv.add(row.min);
        csv.add(row.max);
        csv.add(row.speedup);
        csv.add(row.efficiency);
    }
    if (auto valid = csv.validate(); !valid) {
        return std::move(valid).error();
    }
    return csv.str();
}

std::string benchmarkMarkdown(const BenchmarkReport& r, const ReportProvenance& prov) {
    std::string m = "# UniForge multicore benchmark (Task D)\n\n";
    if (!prov.status.empty()) {
        m += "> **" + prov.status + "**\n\n";
    }
    m += "Generated " + prov.generatedUtc + " by `" + prov.tool + "` " + prov.toolVersion +
         " (commit " + prov.gitCommit + ").  \n";
    m += "Regenerate: `" + prov.command + "`\n\n";

    m += "## Machine and build\n\n| Item | Value |\n|---|---|\n";
    m += "| CPU | " + orUnknown(r.sys.cpu) + " |\n";
    m += "| Cores | " + std::to_string(r.sys.physicalCores) + " physical, " +
         std::to_string(r.sys.logicalCpus) + " logical" +
         (r.sys.coreTypes.empty() ? std::string() : "; " + r.sys.coreTypes) + " |\n";
    m += "| RAM | " + gb(r.sys.ramBytes) + " |\n";
    m += "| OS | " + orUnknown(r.sys.os) + " |\n";
    m += "| Compiler | " + orUnknown(r.sys.compiler) + " |\n";
    m += "| Flags | `" + orUnknown(r.sys.flags) + "` |\n";
    m += "| Build | " + r.sys.buildType + " |\n\n";

    m += "## Workload\n\n| Item | Value |\n|---|---|\n";
    m += "| Cases | " + std::to_string(r.cfg.cases) + " independent rods (k ~ U[5, 50] W/(m·K), "
         "q ~ U[1e3, 5e4] W/m³; L = 1 m, A = 1e-4 m², T0 = 100 °C, TL = 20 °C) |\n";
    m += "| Elements per case | " + std::to_string(r.cfg.elements) + " |\n";
    m += "| Seed | " + std::to_string(r.cfg.seed) + " |\n";
    m += "| Workers | requested " + intsText(r.cfg.workers) + "; run " +
         intsText(r.effectiveWorkers) + " |\n";
    m += "| Protocol | " + std::to_string(kWarmupReps) + " warm-up + " +
         std::to_string(kTimedReps) + " timed repetitions per p; median, min, max |\n";
    m += "| Result set | " + fmt("%.1f", r.resultSetBytes / 1e6) + " MB (preallocated) |\n\n";

    m += "## Results\n\n| p | threads | median (s) | min (s) | max (s) | speedup T1/Tp | "
         "efficiency |\n|---:|---:|---:|---:|---:|---:|---:|\n";
    for (const BenchRow& row : r.rows) {
        m += "| " + std::to_string(row.workers) + " | " + std::to_string(row.threads) + " | " +
             fmt("%.4f", row.median) + " | " + fmt("%.4f", row.min) + " | " +
             fmt("%.4f", row.max) + " | " +
             (r.speedupAssessable ? fmt("%.2f", row.speedup) : std::string("n/a")) + " | " +
             (r.speedupAssessable ? fmt("%.0f %%", row.efficiency * 100.0) : std::string("n/a")) +
             " |\n";
    }
    m += "\nAll repetitions (s):\n\n| p | warm-up |";
    for (int i = 1; i <= kTimedReps; ++i) {
        m += " rep " + std::to_string(i) + " |";
    }
    m += "\n|---:|---:|";
    for (int i = 0; i < kTimedReps; ++i) {
        m += "---:|";
    }
    m += "\n";
    for (const BenchRow& row : r.rows) {
        m += "| " + std::to_string(row.workers) + " | " + fmt("%.4f", row.warmupSec) + " |";
        for (const double s : row.repsSec) {
            m += " " + fmt("%.4f", s) + " |";
        }
        m += "\n";
    }

    m += "\n## Agreement\n\n";
    m += "- max |T_serial − T_parallel| over all nodes of all cases: **" +
         fmt("%.3g", r.maxAbsDiff_C) + " °C** (required ≤ 1e-8 °C): " +
         (r.agreementPass ? "PASS" : "FAIL") + "\n";
    m += "- Index order preserved (out[i] ↔ case i): " +
         std::string(r.orderPreserved ? "yes" : "NO") + "\n";
    m += "\n## Notes\n\n";
    for (const std::string& note : r.notes) {
        m += "- " + note + "\n";
    }
    return m;
}

std::vector<std::pair<std::string, PlotSpec>> benchmarkPlots(const BenchmarkReport& r) {
    std::vector<double> p, median, lo, hi, ideal, speedup, efficiency;
    for (const BenchRow& row : r.rows) {
        p.push_back(static_cast<double>(row.workers));
        median.push_back(row.median);
        lo.push_back(row.min);
        hi.push_back(row.max);
        ideal.push_back(r.rows.front().median / row.workers);
        speedup.push_back(row.speedup);
        efficiency.push_back(row.efficiency * 100.0);
    }
    const std::string workload = " (" + std::to_string(r.cfg.cases) + " cases x " +
                                 std::to_string(r.cfg.elements) + " elements)";

    PlotSpec timing;
    timing.title = "Batch compute time vs workers" + workload;
    timing.xLabel = "workers p";
    timing.yLabel = "compute time (s)";
    timing.series.push_back({"median of 5", p, median, plotcolor::kSeries[0], true, false,
                             PlotMarker::Circle, 2.0});
    timing.series.push_back(
        {"min", p, lo, plotcolor::kSeries[1], true, false, PlotMarker::Triangle, 1.0});
    timing.series.push_back(
        {"max", p, hi, plotcolor::kSeries[2], true, false, PlotMarker::Square, 1.0});
    timing.series.push_back(
        {"ideal T1/p", p, ideal, plotcolor::kReference, true, true, PlotMarker::None, 1.5});

    PlotSpec speed;
    speed.title = "Speedup T1/Tp" + workload;
    speed.xLabel = "workers p";
    speed.yLabel = "speedup (x)";
    speed.series.push_back({"measured (medians)", p, speedup, plotcolor::kSeries[0], true, false,
                            PlotMarker::Circle, 2.0});
    speed.series.push_back(
        {"ideal = p", p, p, plotcolor::kReference, true, true, PlotMarker::None, 1.5});

    PlotSpec eff;
    eff.title = "Parallel efficiency speedup/p" + workload;
    eff.xLabel = "workers p";
    eff.yLabel = "efficiency (%)";
    eff.series.push_back({"measured", p, efficiency, plotcolor::kSeries[0], true, false,
                          PlotMarker::Circle, 2.0});
    eff.series.push_back({"ideal 100 %", p, std::vector<double>(p.size(), 100.0),
                          plotcolor::kReference, true, true, PlotMarker::None, 1.5});
    return {{"_timing.svg", timing}, {"_speedup.svg", speed}, {"_efficiency.svg", eff}};
}

Result<void> saveBenchmarkReport(const BenchmarkReport& r, const ReportProvenance& prov,
                                 const std::filesystem::path& dir, const std::string& stem) {
    auto csv = benchmarkCsv(r);
    if (!csv) {
        return std::move(csv).error();
    }
    if (auto w = writeFileAtomic(dir / (stem + ".json"), benchmarkJson(r, prov)); !w) {
        return w;
    }
    if (auto w = writeFileAtomic(dir / (stem + ".csv"), csv.value()); !w) {
        return w;
    }
    if (auto w = writeFileAtomic(dir / (stem + ".md"), benchmarkMarkdown(r, prov)); !w) {
        return w;
    }
    // A single p (e.g. only the serial baseline) has nothing to plot against.
    for (const auto& [suffix, spec] : r.rows.size() < 2 ? decltype(benchmarkPlots(r)){}
                                                         : benchmarkPlots(r)) {
        if (auto w = saveSvgPlot(spec, dir / (stem + suffix)); !w) {
            return w;
        }
    }

    MetaJson params;
    params.set("seed", std::to_string(r.cfg.seed))
        .set("cases", r.cfg.cases)
        .set("elements", r.cfg.elements)
        .set("requested_workers", r.cfg.workers)
        .set("effective_workers", r.effectiveWorkers)
        .set("warmup_reps", kWarmupReps)
        .set("timed_reps", kTimedReps);
    MetaJson units;
    units.set("workers", "p, worker threads requested")
        .set("threads", "threads started")
        .set("warmup_s", "s, untimed for statistics")
        .set("repN_s", "s, compute-only wall time (steady_clock)")
        .set("median_s", "s")
        .set("speedup", "T1 / Tp (medians)")
        .set("efficiency", "speedup / p");
    MetaJson meta;
    meta.set("file", stem + ".csv")
        .set("description", "Task D serial vs parallel batch benchmark")
        .set("status", prov.status)
        .set("tool", prov.tool)
        .set("tool_version", prov.toolVersion)
        .set("git_commit", prov.gitCommit)
        .set("generated_utc", prov.generatedUtc)
        .set("regenerate", prov.command)
        .set("cpu", r.sys.cpu)
        .set("logical_cpus", r.sys.logicalCpus)
        .set("compiler", r.sys.compiler)
        .set("flags", r.sys.flags)
        .set("params", params)
        .set("units", units)
        .set("max_abs_diff_C", r.maxAbsDiff_C)
        .set("agreement_pass", r.agreementPass);
    return meta.save(dir / (stem + ".meta.json"));
}

} // namespace uniforge::perf
