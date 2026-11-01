#include "cli/commands/BenchCommand.hpp"

#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "core/common/MetaJson.hpp"
#include "core/io/TextParse.hpp"
#include "core/perf/BatchGenerator.hpp"
#include "core/perf/Benchmark.hpp"
#include "core/perf/BenchmarkReportIO.hpp"

namespace uniforge::cli {

namespace {

namespace fs = std::filesystem;

std::string fmt(const char* format, double v) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, format, v);
    return buffer;
}

bool parseInt(const std::string& text, std::int64_t lo, std::int64_t hi, std::int64_t& out) {
    return io::parseInt64(io::trimBlanks(text), out) == io::NumberStatus::Ok && out >= lo &&
           out <= hi;
}

bool parseWorkers(const std::string& text, std::vector<int>& workers) {
    workers.clear();
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        std::int64_t v = 0;
        if (!parseInt(item, 1, perf::kMaxBenchWorkers, v)) {
            return false;
        }
        workers.push_back(static_cast<int>(v));
    }
    return !workers.empty();
}

std::string joinInts(const std::vector<int>& values) {
    std::string s;
    for (const int v : values) {
        s += (s.empty() ? "" : ",") + std::to_string(v);
    }
    return s;
}

// Builds the configuration from the preset and the overrides. Returns false after printing the
// problem on ctx.err.
bool buildConfig(const CommandContext& ctx, perf::BenchmarkConfig& cfg, std::string& command) {
    const BenchArgs& a = ctx.bench;
    if (a.quick && a.report) {
        ctx.err << "error: --quick and --report are mutually exclusive\n";
        return false;
    }
    cfg = a.quick ? perf::quickPreset() : perf::reportPreset();
    bool overridden = false;
    std::int64_t v = 0;
    if (!a.seed.empty()) {
        std::uint64_t seed = 0;
        std::istringstream in(a.seed);
        if (!(in >> seed) || !in.eof() || a.seed.find('-') != std::string::npos) {
            ctx.err << "error: --seed must be a non-negative integer\n";
            return false;
        }
        cfg.seed = seed;
        overridden = true;
    }
    if (!a.cases.empty()) {
        if (!parseInt(a.cases, 1, perf::kMaxBatchCases, v)) {
            ctx.err << "error: --cases must be between 1 and " << perf::kMaxBatchCases << "\n";
            return false;
        }
        cfg.cases = static_cast<int>(v);
        overridden = true;
    }
    if (!a.elements.empty()) {
        if (!parseInt(a.elements, 1, thermal::RodProblem::kMaxElements, v)) {
            ctx.err << "error: --elements must be between 1 and "
                    << thermal::RodProblem::kMaxElements << "\n";
            return false;
        }
        cfg.elements = static_cast<int>(v);
        overridden = true;
    }
    if (!a.workers.empty()) {
        if (!parseWorkers(a.workers, cfg.workers)) {
            ctx.err << "error: --workers must be comma-separated counts between 1 and "
                    << perf::kMaxBenchWorkers << ", e.g. 1,2,4\n";
            return false;
        }
        overridden = true;
    }
    if (!overridden) {
        command = std::string("uniforge_cli bench ") + (a.quick ? "--quick" : "--report");
    } else {
        command = "uniforge_cli bench --seed " + std::to_string(cfg.seed) + " --cases " +
                  std::to_string(cfg.cases) + " --elements " + std::to_string(cfg.elements) +
                  " --workers " + joinInts(cfg.workers);
    }
    return true;
}

void printReport(const CommandContext& ctx, const perf::BenchmarkReport& r) {
    const perf::SystemInfo& s = r.sys;
    ctx.out << "\n  CPU       " << (s.cpu.empty() ? "unknown" : s.cpu) << "\n  Cores     "
            << s.physicalCores << " physical, " << s.logicalCpus << " logical"
            << (s.coreTypes.empty() ? "" : " (" + s.coreTypes + ")") << "\n  RAM       "
            << fmt("%.1f GB", static_cast<double>(s.ramBytes) / (1024.0 * 1024.0 * 1024.0))
            << "\n  OS        " << s.os << "\n  Compiler  " << s.compiler << " (" << s.buildType
            << ")\n  Flags     " << s.flags << "\n\n";
    ctx.out << "     p   median s     min s     max s   speedup   efficiency\n";
    for (const perf::BenchRow& row : r.rows) {
        char line[160];
        std::snprintf(line, sizeof line, "  %4d  %9.4f %9.4f %9.4f   %7.2f   %8.0f %%\n",
                      row.workers, row.median, row.min, row.max, row.speedup,
                      row.efficiency * 100.0);
        ctx.out << line;
    }
    ctx.out << "\n  max |T_serial - T_parallel| = " << fmt("%.3g", r.maxAbsDiff_C)
            << " C (<= 1e-8 required): " << (r.agreementPass ? "PASS" : "FAIL")
            << "\n  index order preserved:        " << (r.orderPreserved ? "PASS" : "FAIL")
            << "\n";
    for (const std::string& note : r.notes) {
        ctx.out << "  note: " << note << "\n";
    }
}

int runWith(const CommandContext& ctx, const perf::BenchmarkConfig& cfg,
            const std::string& command, const fs::path& dir, const std::string& stem,
            const std::string& status) {
    ctx.out << "UniForge benchmark (Task D): " << cfg.cases << " cases x " << cfg.elements
            << " elements, seed " << cfg.seed << ", p = " << joinInts(cfg.workers) << ", "
            << perf::kWarmupReps << " warm-up + " << perf::kTimedReps << " timed reps per p\n";
    std::string lastStage;
    const auto progress = [&](double, std::string_view stage) {
        // Called on the benchmark thread between repetitions only, so plain output is fine.
        if (stage != lastStage) {
            lastStage = std::string(stage);
            ctx.out << "  " << lastStage << "\n" << std::flush;
        }
    };
    const auto result = perf::benchmark(cfg, progress);
    if (!result) {
        ctx.err << "error: " << result.error().message << "\n";
        if (!result.error().hint.empty()) {
            ctx.err << "hint: " << result.error().hint << "\n";
        }
        return result.error().code == ErrorCode::InvalidInput ||
                       result.error().code == ErrorCode::LimitExceeded
                   ? kExitUsage
                   : kExitError;
    }
    const perf::BenchmarkReport& r = result.value();
    printReport(ctx, r);

    perf::ReportProvenance prov;
    prov.tool = "uniforge_cli bench";
    prov.toolVersion = ctx.version;
    prov.gitCommit = ctx.gitCommit;
    prov.generatedUtc = formatUtcIso8601(ctx.startedAt);
    prov.command = command;
    prov.status = status;
    if (auto saved = perf::saveBenchmarkReport(r, prov, dir, stem); !saved) {
        ctx.err << "error: " << saved.error().message << "\n";
        return kExitError;
    }
    ctx.out << "\nWrote " << (dir / (stem + ".{json,csv,md}")).generic_u8string()
            << " (+ .meta.json)\n";
    return r.agreementPass && r.orderPreserved ? kExitOk : kExitCheckFailed;
}

} // namespace

int runBench(const CommandContext& ctx) {
    perf::BenchmarkConfig cfg;
    std::string command;
    if (!buildConfig(ctx, cfg, command)) {
        return kExitUsage;
    }
    const std::string date = formatUtcIso8601(ctx.startedAt).substr(0, 10);
    return runWith(ctx, cfg, command, ctx.outDir / "benchmarks", "bench_" + date,
                   ctx.bench.status);
}

int runQuickBench(const CommandContext& ctx) {
    return runWith(ctx, perf::quickPreset(), "uniforge_cli bench --quick",
                   ctx.outDir / "benchmarks" / "quick", "bench_quick",
                   "QUICK smoke run from `uniforge_cli all` (machine-specific, not the report)");
}

} // namespace uniforge::cli
