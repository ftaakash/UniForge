#include "cli/commands/ImportBenchCommand.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "cli/platform/Process.hpp"
#include "core/common/AtomicFile.hpp"
#include "core/common/CsvWriter.hpp"
#include "core/common/MetaJson.hpp"
#include "core/io/ImportLimits.hpp"
#include "core/io/StlParser.hpp"
#include "core/io/TextParse.hpp"
#include "core/io/ThermalCsvParser.hpp"

namespace uniforge::cli {

namespace {

namespace fs = std::filesystem;

constexpr int kReps = 3;
const std::vector<std::size_t> kDefaultSizes{10'000, 100'000, 1'000'000};

// Bytes of the parsed result per element: StlMesh keeps 3 vertices + 1 normal (4 x 24 B);
// ThermalNodeSet keeps a 40 B node plus an 8 B line number used for duplicate messages.
constexpr double kStlBytesPerFacet = 96.0;
constexpr double kCsvBytesPerNode = 48.0;

std::string fmt(const char* format, double v) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, format, v);
    return buffer;
}

double mb(double bytes) {
    return bytes / 1e6;
}

std::string compilerName() {
#if defined(_MSC_FULL_VER)
    return "MSVC " + std::to_string(_MSC_FULL_VER / 10'000'000) + "." +
           std::to_string((_MSC_FULL_VER / 100'000) % 100) + "." +
           std::to_string(_MSC_FULL_VER % 100'000);
#elif defined(__clang__)
    return "Clang " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__);
#elif defined(__GNUC__)
    return "GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#else
    return "unknown compiler";
#endif
}

std::string buildType() {
#if defined(NDEBUG)
    return "Release (optimised)";
#else
    return "Debug (not optimised: timings are not representative)";
#endif
}

// Streams `text` to the file in ~1 MB pieces so generating a 250 MB file needs little memory.
class ChunkedFile {
public:
    explicit ChunkedFile(const fs::path& path)
        : out_(path, std::ios::binary) {}
    std::string& buffer() { return buffer_; }
    void maybeFlush() {
        if (buffer_.size() >= (1U << 20)) {
            flush();
        }
    }
    bool finish() {
        flush();
        out_.close();
        return !out_.fail();
    }

private:
    void flush() {
        out_.write(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
        buffer_.clear();
    }
    std::ofstream out_;
    std::string buffer_;
};

// A flat grid of right triangles, two per 1 mm cell, written the way CAD exporters do
// ("%.6e", one value per field), so the file size per facet is realistic (~250 bytes).
bool writeBenchStl(const fs::path& path, std::size_t facets) {
    ChunkedFile file(path);
    std::string& b = file.buffer();
    b += "solid import_bench\n";
    std::size_t cols = 1;
    while (cols * cols * 2 < facets) {
        ++cols;
    }
    char line[160];
    const auto vertex = [&](std::size_t cx, std::size_t cy, double z) {
        std::snprintf(line, sizeof line, "      vertex %.6e %.6e %.6e\n",
                      static_cast<double>(cx) * 1e-3, static_cast<double>(cy) * 1e-3, z);
        b += line;
    };
    for (std::size_t f = 0; f < facets; ++f) {
        const std::size_t cell = f / 2;
        const std::size_t cx = cell % cols;
        const std::size_t cy = cell / cols;
        const double z = static_cast<double>(f % 7) * 1e-4;
        b += "  facet normal 0.000000e+00 0.000000e+00 1.000000e+00\n    outer loop\n";
        if (f % 2 == 0) {
            vertex(cx, cy, z);
            vertex(cx + 1, cy, z);
            vertex(cx, cy + 1, z);
        } else {
            vertex(cx + 1, cy, z);
            vertex(cx + 1, cy + 1, z);
            vertex(cx, cy + 1, z);
        }
        b += "    endloop\n  endfacet\n";
        file.maybeFlush();
    }
    b += "endsolid import_bench\n";
    return file.finish();
}

// Nodes on a 1000 x 1000 x n/10^6 grid with 1 mm spacing; T varies smoothly.
bool writeBenchCsv(const fs::path& path, std::size_t nodes) {
    ChunkedFile file(path);
    std::string& b = file.buffer();
    b += "node_id,x_m,y_m,z_m,temperature_C\n";
    char line[160];
    for (std::size_t i = 0; i < nodes; ++i) {
        std::snprintf(line, sizeof line, "%zu,%.6e,%.6e,%.6e,%.4f\n", i + 1,
                      static_cast<double>(i % 1000) * 1e-3,
                      static_cast<double>((i / 1000) % 1000) * 1e-3,
                      static_cast<double>(i / 1'000'000) * 1e-3,
                      20.0 + static_cast<double>(i % 100'000) * 1e-3);
        b += line;
        file.maybeFlush();
    }
    return file.finish();
}

bool parseSizes(const std::string& text, std::vector<std::size_t>& sizes) {
    sizes.clear();
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        std::int64_t v = 0;
        if (io::parseInt64(io::trimBlanks(item), v) != io::NumberStatus::Ok || v < 1 ||
            v > 100'000'000) {
            return false;
        }
        sizes.push_back(static_cast<std::size_t>(v));
    }
    return !sizes.empty();
}

using Fields = std::map<std::string, std::string>;

Fields readFields(const fs::path& path) {
    Fields fields;
    std::ifstream in(path, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq != std::string::npos) {
            fields[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
    return fields;
}

double number(const Fields& f, const std::string& key) {
    const auto it = f.find(key);
    double v = std::numeric_limits<double>::quiet_NaN();
    if (it != f.end()) {
        (void)io::parseDouble(it->second, v);
    }
    return v;
}

struct Row {
    std::string kind; // "stl" or "csv"
    std::size_t count = 0;
    double fileBytes = 0;
    double parseMedian = 0;
    double parseMin = 0;
    double baselineWs = 0;
    double peakWs = 0;
    double peakPrivate = 0; // NaN when the platform does not report it
};

void writeMarkdown(const CommandContext& ctx, const std::vector<Row>& rows, std::string& md) {
    const io::ImportLimits limits;
    md += "# Import size limits (measured)\n\n";
    md += "Generated by `uniforge_cli import-bench`. Each run parses one temporary file in a fresh "
          "child process, " +
          std::to_string(kReps) +
          " times; the table shows the median parse time and the peak memory of that process. "
          "Files are written just before the run (so they are read from the OS file cache) and "
          "deleted right after. Machine-specific numbers: rerun the command to measure another "
          "PC.\n\n";
    md += "| | |\n|---|---|\n";
    md += "| CPU | " + platform::cpuName() + " (" +
          std::to_string(std::thread::hardware_concurrency()) + " logical CPUs) |\n";
    md += "| RAM | " + fmt("%.1f", static_cast<double>(platform::totalRamBytes()) / (1ULL << 30)) +
          " GiB |\n";
    md += "| OS | " + platform::osName() + " |\n";
    md += "| Compiler | " + compilerName() + ", " + buildType() + " |\n";
    md += "| Tool | uniforge_cli " + ctx.version + " (" + ctx.gitCommit + "), " +
          formatUtcIso8601(ctx.startedAt) + " |\n\n";

    md += "| File | Elements | File size (MB) | Parse time (s, median) | Throughput (MB/s) | "
          "Peak working set (MB) | Peak private (MB) | Above baseline (MB) | Result data (MB) "
          "|\n";
    md += "|---|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    const auto limitOf = [&](const std::string& kind) {
        return kind == "stl" ? limits.maxFacets : limits.maxNodes;
    };
    for (const Row& r : rows) {
        const double data =
            static_cast<double>(r.count) * (r.kind == "stl" ? kStlBytesPerFacet : kCsvBytesPerNode);
        md += "| " + std::string(r.kind == "stl" ? "ASCII STL (facets)" : "CSV (nodes)") + " | " +
              io::formatCount(r.count) + (r.count == limitOf(r.kind) ? " (limit)" : "") + " | " +
              fmt("%.1f", mb(r.fileBytes)) + " | " +
              fmt("%.3f", r.parseMedian) + " | " + fmt("%.0f", mb(r.fileBytes) / r.parseMedian) +
              " | " + fmt("%.1f", mb(r.peakWs)) + " | " +
              (std::isnan(r.peakPrivate) ? std::string("n/a") : fmt("%.1f", mb(r.peakPrivate))) +
              " | " + fmt("%.1f", mb(r.peakWs - r.baselineWs)) + " | " + fmt("%.1f", mb(data)) +
              " |\n";
    }

    md += "\n## Limits enforced by the importers (`core/io/ImportLimits.hpp`)\n\n";
    md += "| Limit | Value |\n|---|---:|\n";
    md += "| File size | " + io::formatMegabytes(limits.maxBytes) + " |\n";
    md += "| STL facets | " + io::formatCount(limits.maxFacets) + " |\n";
    md += "| CSV nodes | " + io::formatCount(limits.maxNodes) + " |\n";
    md += "| Line length | " + io::formatCount(limits.maxLineBytes) + " characters |\n\n";

    // At each limit: the measured run if there is one, otherwise a linear extrapolation from the
    // largest run (clearly labelled as not measured).
    for (const std::string kind : {"stl", "csv"}) {
        const Row* largest = nullptr;
        for (const Row& r : rows) {
            if (r.kind == kind && (largest == nullptr || r.count > largest->count)) {
                largest = &r;
            }
        }
        if (largest == nullptr) {
            continue;
        }
        const std::size_t limit = limitOf(kind);
        const double scale = static_cast<double>(limit) / static_cast<double>(largest->count);
        const bool measured = largest->count == limit;
        md += "- " + std::string(kind == "stl" ? "STL" : "CSV") + " at the limit (" +
              io::formatCount(limit) + (kind == "stl" ? " facets" : " nodes") + "), " +
              (measured ? std::string("measured")
                        : "extrapolated linearly from " + io::formatCount(largest->count) +
                              " (not measured)") +
              ": " + fmt("%.0f", mb(largest->fileBytes) * scale) + " MB file, " +
              fmt("%.2f", largest->parseMedian * scale) + " s to parse, " +
              fmt("%.0f", mb(largest->peakWs) * scale) + " MB peak working set.\n";
    }
    md += "\nRegenerate with `app\\uniforge_cli.exe import-bench` (writes this file and "
          "`import_limits.csv`).\n";
}

} // namespace

int runImportBenchWorker(const CommandContext& ctx) {
    if (ctx.args.size() != 3 || (ctx.args[0] != "stl" && ctx.args[0] != "csv")) {
        ctx.err << "usage: import-bench-worker <stl|csv> FILE RESULT\n";
        return kExitUsage;
    }
    const bool stl = ctx.args[0] == "stl";
    const fs::path file = fs::u8path(ctx.args[1]);
    const fs::path result = fs::u8path(ctx.args[2]);
    io::ImportLimits unlimited;
    unlimited.maxBytes = std::numeric_limits<std::uintmax_t>::max();
    unlimited.maxFacets = std::numeric_limits<std::size_t>::max();
    unlimited.maxNodes = std::numeric_limits<std::size_t>::max();

    const platform::MemoryUsage before = platform::processMemory();
    std::string out = "baseline_ws=" + std::to_string(before.workingSetBytes) + "\n";
    std::vector<double> seconds;
    std::size_t count = 0;
    std::uintmax_t bytes = 0;
    for (int rep = 0; rep < kReps; ++rep) {
        if (stl) {
            const auto r = io::parseAsciiStlFile(file, unlimited);
            if (!r) {
                out += "error=" + r.error().message + "\n";
                break;
            }
            count = r.value().tri.size();
            bytes = r.value().stats.bytes;
            seconds.push_back(r.value().stats.parseSeconds);
        } else {
            const auto r = io::parseThermalCsvFile(file, unlimited);
            if (!r) {
                out += "error=" + r.error().message + "\n";
                break;
            }
            count = r.value().nodes.size();
            bytes = r.value().stats.bytes;
            seconds.push_back(r.value().stats.parseSeconds);
        }
    }
    const platform::MemoryUsage after = platform::processMemory();
    std::sort(seconds.begin(), seconds.end());
    out += "ok=" + std::string(seconds.size() == kReps ? "1" : "0") + "\n";
    out += "count=" + std::to_string(count) + "\nbytes=" + std::to_string(bytes) + "\n";
    if (!seconds.empty()) {
        out += "parse_min_s=" + formatShortest(seconds.front()) +
               "\nparse_median_s=" + formatShortest(seconds[seconds.size() / 2]) + "\n";
    }
    out += "peak_ws=" + std::to_string(after.peakWorkingSetBytes) + "\n";
    if (after.hasPrivate) {
        out += "peak_private=" + std::to_string(after.peakPrivateBytes) + "\n";
    }
    if (auto w = writeFileAtomic(result, out); !w) {
        ctx.err << "error: " << w.error().message << "\n";
        return kExitError;
    }
    return seconds.size() == kReps ? kExitOk : kExitError;
}

int runImportBench(const CommandContext& ctx) {
    std::vector<std::size_t> sizes = kDefaultSizes;
    if (!ctx.sizes.empty() && !parseSizes(ctx.sizes, sizes)) {
        ctx.err << "error: --sizes must be comma-separated counts between 1 and 100000000\n";
        return kExitUsage;
    }
    const fs::path exe = platform::currentExecutable();
    std::error_code ec;
    const fs::path temp =
        fs::temp_directory_path(ec) /
        ("uniforge_import_bench_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (ec || !fs::create_directories(temp, ec)) {
        ctx.err << "error: cannot create a temporary folder for the benchmark files\n";
        return kExitError;
    }
    ctx.out << "UniForge import benchmark: " << kReps << " parses per file, each file in a fresh "
            << "process\n  temporary files: " << temp.filename().u8string()
            << " in the system temp folder (deleted afterwards)\n";

    // Every size for both formats; by default also one run of each format exactly at its
    // ImportLimits default, so the enforced limits are measured, not extrapolated.
    std::vector<std::pair<std::string, std::size_t>> jobs;
    for (const std::size_t n : sizes) {
        jobs.emplace_back("stl", n);
        jobs.emplace_back("csv", n);
    }
    if (ctx.sizes.empty()) {
        const io::ImportLimits limits;
        jobs.emplace_back("stl", limits.maxFacets);
        jobs.emplace_back("csv", limits.maxNodes);
    }

    std::vector<Row> rows;
    int exit = kExitOk;
    for (const auto& [kind, n] : jobs) {
        const bool stl = kind == "stl";
        const fs::path file = temp / ("bench_" + std::to_string(n) + "." + kind);
        const fs::path result = temp / "result.txt";
        ctx.out << "  " << kind << " " << io::formatCount(n) << (stl ? " facets" : " nodes")
                << ": writing..." << std::flush;
        const bool written = stl ? writeBenchStl(file, n) : writeBenchCsv(file, n);
        const double fileBytes = static_cast<double>(fs::file_size(file, ec));
        if (!written || ec) {
            ctx.out << " FAILED (disk full?)\n";
            exit = kExitError;
            fs::remove(file, ec);
            continue;
        }
        ctx.out << " " << fmt("%.1f", mb(fileBytes)) << " MB, parsing..." << std::flush;
        const int code = platform::runProcess(
            exe, {"import-bench-worker", kind, file.u8string(), result.u8string()});
        const Fields f = readFields(result);
        fs::remove(file, ec);
        fs::remove(result, ec);
        if (code != 0 || f.count("ok") == 0 || f.at("ok") != "1") {
            ctx.out << " FAILED (exit " << code << ")"
                    << (f.count("error") ? ": " + f.at("error") : std::string()) << "\n";
            exit = kExitError;
            continue;
        }
        Row r;
        r.kind = kind;
        r.count = static_cast<std::size_t>(number(f, "count"));
        r.fileBytes = fileBytes;
        r.parseMedian = number(f, "parse_median_s");
        r.parseMin = number(f, "parse_min_s");
        r.baselineWs = number(f, "baseline_ws");
        r.peakWs = number(f, "peak_ws");
        r.peakPrivate = number(f, "peak_private");
        ctx.out << " " << fmt("%.3f", r.parseMedian) << " s, peak " << fmt("%.1f", mb(r.peakWs))
                << " MB\n";
        rows.push_back(r);
    }
    fs::remove_all(temp, ec);
    if (rows.empty()) {
        return kExitError;
    }

    const std::vector<std::string> cols{"kind",
                                        "elements",
                                        "file_bytes",
                                        "parse_median_s",
                                        "parse_min_s",
                                        "throughput_MB_per_s",
                                        "baseline_ws_bytes",
                                        "peak_ws_bytes",
                                        "peak_private_bytes"};
    CsvWriter csv(cols, CsvNumberFormat::Shortest);
    for (const Row& r : rows) {
        csv.beginRow();
        csv.add(r.kind);
        csv.add(static_cast<long long>(r.count));
        csv.add(r.fileBytes);
        csv.add(r.parseMedian);
        csv.add(r.parseMin);
        csv.add(mb(r.fileBytes) / r.parseMedian);
        csv.add(r.baselineWs);
        csv.add(r.peakWs);
        if (std::isnan(r.peakPrivate)) {
            csv.addBlank();
        } else {
            csv.add(r.peakPrivate);
        }
    }
    const fs::path dir = ctx.outDir / "benchmarks";
    const fs::path csvPath = dir / "import_limits.csv";
    if (auto w = csv.save(csvPath); !w) {
        ctx.err << "error: " << w.error().message << "\n";
        return kExitError;
    }
    MetaJson units;
    units.set("kind", "stl = ASCII STL facets, csv = thermal CSV nodes")
        .set("elements", "facets or nodes")
        .set("file_bytes", "bytes")
        .set("parse_median_s", "s, median of " + std::to_string(kReps) + " parses")
        .set("parse_min_s", "s")
        .set("throughput_MB_per_s", "10^6 bytes per second")
        .set("baseline_ws_bytes", "bytes, worker working set before the first parse")
        .set("peak_ws_bytes", "bytes, worker peak working set (resident)")
        .set("peak_private_bytes", "bytes, worker peak committed private memory (Windows)");
    MetaJson meta;
    meta.set("file", "import_limits.csv")
        .set("description", "Measured ASCII STL / thermal CSV import time and peak memory")
        .set("tool", "uniforge_cli import-bench")
        .set("tool_version", ctx.version)
        .set("git_commit", ctx.gitCommit)
        .set("generated_utc", formatUtcIso8601(ctx.startedAt))
        .set("cpu", platform::cpuName())
        .set("logical_cpus", static_cast<int>(std::thread::hardware_concurrency()))
        .set("ram_bytes", static_cast<double>(platform::totalRamBytes()))
        .set("os", platform::osName())
        .set("compiler", compilerName())
        .set("build", buildType())
        .set("repetitions", kReps)
        .set("columns", cols)
        .set("units", units);
    if (auto w = meta.save(dir / "import_limits.meta.json"); !w) {
        ctx.err << "error: " << w.error().message << "\n";
        return kExitError;
    }
    std::string md;
    writeMarkdown(ctx, rows, md);
    if (auto w = writeFileAtomic(dir / "import_limits.md", md); !w) {
        ctx.err << "error: " << w.error().message << "\n";
        return kExitError;
    }
    ctx.out << "\nWrote " << csvPath.generic_u8string() << " (+ .meta.json) and "
            << (dir / "import_limits.md").generic_u8string() << "\n";
    return exit;
}

} // namespace uniforge::cli
