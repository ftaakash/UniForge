#include "cli/commands/SamplesCommand.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "core/common/AtomicFile.hpp"
#include "core/common/CsvWriter.hpp"
#include "core/io/StlParser.hpp"
#include "core/io/StlWriter.hpp"
#include "core/io/TextParse.hpp"
#include "core/io/ThermalCsvParser.hpp"
#include "core/thermal/Analytical.hpp"

namespace uniforge::cli {

namespace {

namespace fs = std::filesystem;
using io::Vec3d;

// --- Sample definitions (documented in data/PROVENANCE.md) ---------------------------------
// Rod of Task C: x in [0, L], square cross-section 10 mm x 10 mm centred on the x axis, so
// A = 1e-4 m^2 as in the brief.
constexpr double kRodHalfWidth_m = 0.005;
// Synthetic nodal temperatures: 101 stations along x (every 10 mm) x a 3 x 3 grid over the
// cross-section (y, z in {-5, 0, +5} mm) = 909 nodes, id = 1 + 9*i + 3*j + k.
constexpr int kStations = 101;
constexpr int kSide = 3;
// T = T_exact(x) - kLateralDrop_C * ((y/a)^2 + (z/a)^2) / 2 + noise, noise uniform in
// [-kNoise_C, +kNoise_C) from mt19937_64(kSampleSeed); rounded to 0.0001 °C.
constexpr double kLateralDrop_C = 0.5;
constexpr double kNoise_C = 0.02;
// Rounded as round(T * 1e4) / 1e4: the nearest double to the 4-decimal value, so it prints short.
constexpr double kSteps_per_C = 1e4;
constexpr double kRounding_C = 1.0 / kSteps_per_C;
// Heat sink: base plate 60 x 40 x 4 mm, 7 fins 2 mm thick, 40 mm long, 26 mm tall on top,
// pitch 9 mm starting at x = 3 mm. Each part is a separate closed box (touching, not fused).
constexpr int kFins = 7;

const char* const kRodFile = "rod_10x10mm_1m.stl";
const char* const kHeatsinkFile = "heatsink_fins.stl";
const char* const kCsvFile = "rod_nodal_temperature_SYNTHETIC.csv";

// Millimetres to metres: integer mm / 1000 is the nearest double to the decimal value, so the
// shortest formatting prints "0.012", not "0.012000000000000002".
double mm(int v) {
    return v / 1000.0;
}

// Axis-aligned box as 12 triangles, counter-clockwise seen from outside (outward normals).
void appendBox(std::string& out, const Vec3d& lo, const Vec3d& hi) {
    const auto p = [&](int i, int j, int k) {
        return Vec3d{i != 0 ? hi.x : lo.x, j != 0 ? hi.y : lo.y, k != 0 ? hi.z : lo.z};
    };
    const int faces[6][4][3] = {
        {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}, // -x
        {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}, // +x
        {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, // -y
        {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}, // +y
        {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}, // -z
        {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, // +z
    };
    for (const auto& f : faces) {
        const Vec3d a = p(f[0][0], f[0][1], f[0][2]);
        const Vec3d b = p(f[1][0], f[1][1], f[1][2]);
        const Vec3d c = p(f[2][0], f[2][1], f[2][2]);
        const Vec3d d = p(f[3][0], f[3][1], f[3][2]);
        io::appendStlFacet(out, {a, b, c});
        io::appendStlFacet(out, {a, c, d});
    }
}

std::string rodStl() {
    const thermal::RodProblem rod;
    std::string out;
    io::appendStlBegin(out, "rod_10x10mm_1m");
    appendBox(out, {0.0, -kRodHalfWidth_m, -kRodHalfWidth_m},
              {rod.L, kRodHalfWidth_m, kRodHalfWidth_m});
    io::appendStlEnd(out, "rod_10x10mm_1m");
    return out;
}

std::string heatsinkStl() {
    std::string out;
    io::appendStlBegin(out, "heatsink_fins");
    appendBox(out, {0.0, 0.0, 0.0}, {mm(60), mm(40), mm(4)});
    for (int i = 0; i < kFins; ++i) {
        const int x0 = 3 + 9 * i;
        appendBox(out, {mm(x0), 0.0, mm(4)}, {mm(x0 + 2), mm(40), mm(30)});
    }
    io::appendStlEnd(out, "heatsink_fins");
    return out;
}

// [0, 1) from the top 53 bits: the same on every compiler, unlike
// std::uniform_real_distribution, whose algorithm is implementation-defined.
double unitUniform(std::mt19937_64& rng) {
    return static_cast<double>(rng() >> 11) * 0x1.0p-53;
}

double lateral(double y, double z) {
    const double yh = y / kRodHalfWidth_m;
    const double zh = z / kRodHalfWidth_m;
    return -kLateralDrop_C * (yh * yh + zh * zh) / 2.0;
}

double modelTemperature(double x, double y, double z) {
    return thermal::exactTemperature(thermal::RodProblem{}, x) + lateral(y, z);
}

CsvWriter syntheticCsv() {
    const thermal::RodProblem rod;
    CsvWriter csv({"node_id", "x_m", "y_m", "z_m", "temperature_C"}, CsvNumberFormat::Shortest);
    std::mt19937_64 rng(kSampleSeed);
    for (int i = 0; i < kStations; ++i) {
        const double x = rod.L * i / (kStations - 1);
        for (int j = 0; j < kSide; ++j) {
            for (int k = 0; k < kSide; ++k) {
                const double y = mm(5 * (j - 1));
                const double z = mm(5 * (k - 1));
                const double noise = kNoise_C * (2.0 * unitUniform(rng) - 1.0);
                const double t = modelTemperature(x, y, z) + noise;
                csv.beginRow();
                csv.add(static_cast<long long>(1 + kSide * kSide * i + kSide * j + k));
                csv.add(x);
                csv.add(y);
                csv.add(z);
                csv.add(std::round(t * kSteps_per_C) / kSteps_per_C);
            }
        }
    }
    return csv;
}

// Signed volume enclosed by a closed, outward-oriented mesh (divergence theorem).
double enclosedVolume(const io::StlMesh& m) {
    double v = 0.0;
    for (const auto& t : m.tri) {
        const Vec3d& a = t[0];
        const Vec3d& b = t[1];
        const Vec3d& c = t[2];
        v += a.x * (b.y * c.z - b.z * c.y) - a.y * (b.x * c.z - b.z * c.x) +
             a.z * (b.x * c.y - b.y * c.x);
    }
    return v / 6.0;
}

std::string fmt(const char* format, double v) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, format, v);
    return buffer;
}

std::string bboxText(const Vec3d& lo, const Vec3d& hi) {
    return fmt("%.6g", hi.x - lo.x) + " x " + fmt("%.6g", hi.y - lo.y) + " x " +
           fmt("%.6g", hi.z - lo.z) + " m";
}

bool near(double a, double b, double tol) {
    return std::abs(a - b) <= tol;
}

struct Checker {
    const CommandContext& ctx;
    bool all = true;
    void operator()(bool pass, const std::string& what) {
        ctx.out << (pass ? "  PASS  " : "  FAIL  ") << what << "\n";
        all = all && pass;
    }
};

void printError(const CommandContext& ctx, const std::string& file, const Error& e) {
    ctx.err << "error: " << file << ": [" << toString(e.code) << "] " << e.message
            << "\n  hint: " << e.hint << "\n";
}

} // namespace

int runGenSamples(const CommandContext& ctx) {
    const fs::path geometry = ctx.dataDir / "geometry";
    const fs::path thermalDir = ctx.dataDir / "thermal";
    const fs::path rodPath = geometry / kRodFile;
    const fs::path sinkPath = geometry / kHeatsinkFile;
    const fs::path csvPath = thermalDir / kCsvFile;

    ctx.out << "UniForge sample data (Task B): self-created, deterministic, seed " << kSampleSeed
            << "\n";
    for (const auto& [path, text] :
         {std::pair{rodPath, rodStl()}, std::pair{sinkPath, heatsinkStl()}}) {
        if (auto r = writeFileAtomic(path, text); !r) {
            printError(ctx, path.filename().u8string(), r.error());
            return kExitError;
        }
    }
    if (auto r = syntheticCsv().save(csvPath); !r) {
        printError(ctx, kCsvFile, r.error());
        return kExitError;
    }

    // Read every file back through the importers the GUI uses.
    const auto rod = io::parseAsciiStlFile(rodPath);
    const auto sink = io::parseAsciiStlFile(sinkPath);
    const auto nodes = io::parseThermalCsvFile(csvPath);
    for (const auto& [name, error] : {std::pair{kRodFile, rod ? nullptr : &rod.error()},
                                      std::pair{kHeatsinkFile, sink ? nullptr : &sink.error()},
                                      std::pair{kCsvFile, nodes ? nullptr : &nodes.error()}}) {
        if (error != nullptr) {
            printError(ctx, name, *error);
            return kExitCheckFailed;
        }
    }

    const thermal::RodProblem problem;
    const io::StlMesh& r = rod.value();
    const io::StlMesh& s = sink.value();
    const io::ThermalNodeSet& n = nodes.value();
    const double rodVolume = enclosedVolume(r);
    const double sinkVolume = enclosedVolume(s);
    const double sinkExpected = mm(60) * mm(40) * mm(4) + kFins * mm(2) * mm(40) * mm(26);
    double maxDeviation = 0.0;
    for (const io::ThermalNode& node : n.nodes) {
        maxDeviation = std::max(
            maxDeviation, std::abs(node.T_C - modelTemperature(node.p.x, node.p.y, node.p.z)));
    }
    const thermal::TemperaturePeak peak = thermal::exactPeak(problem);

    ctx.out << "  " << kRodFile << ": " << r.tri.size() << " facets, bbox "
            << bboxText(r.bbMin, r.bbMax) << ", volume " << fmt("%.6g", rodVolume) << " m^3\n"
            << "  " << kHeatsinkFile << ": " << s.tri.size() << " facets, bbox "
            << bboxText(s.bbMin, s.bbMax) << ", volume " << fmt("%.6g", sinkVolume) << " m^3\n"
            << "  " << kCsvFile << ": " << n.nodes.size() << " nodes, T " << fmt("%.4f", n.Tmin)
            << " .. " << fmt("%.4f", n.Tmax) << " C, bbox " << bboxText(n.bbMin, n.bbMax)
            << "\n\nChecks:\n";

    Checker check{ctx};
    check(r.tri.size() == 12 && r.degenerate == 0 && r.stats.warnings.empty(),
          "rod: 12 facets, no degenerate triangles, no warnings");
    check(r.bbMin.x == 0.0 && r.bbMax.x == problem.L && r.bbMin.y == -kRodHalfWidth_m &&
              r.bbMax.y == kRodHalfWidth_m && r.bbMin.z == -kRodHalfWidth_m &&
              r.bbMax.z == kRodHalfWidth_m,
          "rod: bbox exactly [0, 1] x [-0.005, 0.005] x [-0.005, 0.005] m");
    check(near(rodVolume, problem.A * problem.L, 1e-12 * problem.A * problem.L),
          "rod: closed with outward normals, volume = A*L = 1e-4 m^3 (rel. 1e-12)");
    check(s.tri.size() == 12U * (1 + kFins) && s.degenerate == 0,
          "heat sink: " + std::to_string(12 * (1 + kFins)) + " facets, no degenerate triangles");
    check(near(sinkVolume, sinkExpected, 1e-12 * sinkExpected),
          "heat sink: volume = base + fins = " + fmt("%.6g", sinkExpected) + " m^3 (rel. 1e-12)");
    check(n.nodes.size() == static_cast<std::size_t>(kStations * kSide * kSide) && n.syntheticFlag,
          "csv: " + std::to_string(kStations * kSide * kSide) +
              " unique nodes, flagged SYNTHETIC by its file name");
    check(maxDeviation <= kNoise_C + kRounding_C,
          "csv: |T - (T_exact(x) + lateral(y, z))| <= " + fmt("%g", kNoise_C) +
              " C noise + rounding (max " + fmt("%.4f", maxDeviation) + " C)");
    check(near(n.Tmax, peak.T, kNoise_C + kRounding_C),
          "csv: Tmax within the noise of the exact peak " + fmt("%.1f", peak.T) +
              " C at x = " + fmt("%.2f", peak.x) + " m");

    ctx.out << "\nWrote " << rodPath.generic_u8string() << ", " << sinkPath.generic_u8string()
            << " and " << csvPath.generic_u8string() << "\n";
    return check.all ? kExitOk : kExitCheckFailed;
}

int runInspect(const CommandContext& ctx) {
    if (ctx.args.empty()) {
        ctx.err << "error: inspect needs at least one .stl or .csv file\n";
        return kExitUsage;
    }
    int exit = kExitOk;
    for (const std::string& arg : ctx.args) {
        const fs::path path = fs::u8path(arg);
        std::string ext = path.extension().u8string();
        for (char& c : ext) {
            c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }
        const std::string name = path.filename().u8string();
        const io::ImportStats* stats = nullptr;
        std::string summary;
        const Error* error = nullptr;
        Result<io::StlMesh> mesh = makeError(ErrorCode::Internal, "not loaded");
        Result<io::ThermalNodeSet> set = makeError(ErrorCode::Internal, "not loaded");
        if (ext == ".stl") {
            mesh = io::parseAsciiStlFile(path);
            if (mesh) {
                const io::StlMesh& m = mesh.value();
                stats = &m.stats;
                summary = io::countOf(m.tri.size(), "facet", "facets") + ", " +
                          std::to_string(m.degenerate) + " degenerate, bbox " +
                          bboxText(m.bbMin, m.bbMax);
            } else {
                error = &mesh.error();
            }
        } else if (ext == ".csv") {
            set = io::parseThermalCsvFile(path);
            if (set) {
                const io::ThermalNodeSet& s = set.value();
                stats = &s.stats;
                summary = io::countOf(s.nodes.size(), "node", "nodes") + ", T " +
                          fmt("%.6g", s.Tmin) + " .. " + fmt("%.6g", s.Tmax) + " C, bbox " +
                          bboxText(s.bbMin, s.bbMax) + (s.syntheticFlag ? ", SYNTHETIC DATA" : "");
            } else {
                error = &set.error();
            }
        } else {
            ctx.err << name << ": REJECTED  only .stl and .csv files can be imported\n";
            exit = kExitError;
            continue;
        }
        if (error != nullptr) {
            ctx.out << name << ": REJECTED [" << toString(error->code) << "]"
                    << (error->line ? " line " + std::to_string(*error->line) : std::string())
                    << "\n  " << error->message << "\n  hint: " << error->hint << "\n";
            exit = kExitError;
            continue;
        }
        ctx.out << name << ": OK  " << summary << ", " << stats->lines << " lines, " << stats->bytes
                << " bytes, " << fmt("%.3f", stats->parseSeconds * 1e3) << " ms\n";
        for (const std::string& w : stats->warnings) {
            ctx.out << "  warning: " << w << "\n";
        }
    }
    return exit;
}

} // namespace uniforge::cli
