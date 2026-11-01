#include "cli/commands/FeaCommand.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/common/CsvWriter.hpp"
#include "core/common/MetaJson.hpp"
#include "core/common/SvgPlot.hpp"
#include "core/thermal/Analytical.hpp"
#include "core/thermal/ConvergenceStudy.hpp"
#include "core/thermal/ErrorAnalysis.hpp"

namespace uniforge::cli {

namespace {

namespace fs = std::filesystem;
using thermal::RodProblem;
using thermal::StudyMesh;
using thermal::StudyRow;

// Verification tolerances (spec N5).
constexpr double kNodalTol = 1e-10;   // °C
constexpr double kMidpointTol = 1e-9; // °C, |measured − q·h²/(8k)|
constexpr double kEnergyTol = 1e-10;  // W
constexpr double kOrderTol = 1e-6;
constexpr int kExactSamples = 201;

const std::vector<int> kMeshSizes{10, 20, 40, 80};

std::string fmt(const char* format, double v) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, format, v);
    return buffer;
}

MetaJson problemJson(const RodProblem& p) {
    MetaJson j;
    j.set("L_m", p.L)
        .set("A_m2", p.A)
        .set("k_W_per_m_K", p.k)
        .set("q_W_per_m3", p.q)
        .set("T0_C", p.T0)
        .set("TL_C", p.TL);
    return j;
}

// Sidecar next to `csvPath`: <stem>.meta.json.
Result<void> writeSidecar(const CommandContext& ctx, const fs::path& csvPath,
                          const std::string& description, const RodProblem& p,
                          const std::vector<std::string>& columns, const MetaJson& units,
                          const MetaJson* extra = nullptr) {
    MetaJson meta;
    meta.set("file", csvPath.filename().generic_u8string())
        .set("description", description)
        .set("tool", "uniforge_cli fea")
        .set("tool_version", ctx.version)
        .set("git_commit", ctx.gitCommit)
        .set("generated_utc", formatUtcIso8601(ctx.startedAt))
        .set("problem", problemJson(p))
        .set("exact_solution", "T(x) = T0 + (TL - T0) x / L + q / (2 k) x (L - x)")
        .set("element", "two-node linear, consistent load, Dirichlet by symmetric elimination, "
                        "Thomas solve")
        .set("columns", columns)
        .set("units", units)
        .set("number_format", "%.17g");
    if (extra != nullptr) {
        meta.set("details", *extra);
    }
    fs::path sidecar = csvPath;
    sidecar.replace_extension(".meta.json");
    return meta.save(sidecar);
}

Result<void> writeMeshCsvs(const CommandContext& ctx, const RodProblem& base, int n,
                           const StudyMesh& mesh) {
    RodProblem p = base;
    p.elements = n;
    const fs::path csvDir = ctx.outDir / "csv";
    const std::string stem = "fea_N" + std::to_string(n);
    MetaJson details;
    details.set("elements", n).set("h_m", base.L / n);

    const std::vector<std::string> nodeCols{"x_m", "T_fe_C", "T_exact_C", "abs_err_C"};
    CsvWriter nodes(nodeCols);
    for (std::size_t i = 0; i < mesh.solution.x.size(); ++i) {
        nodes.beginRow();
        nodes.add(mesh.solution.x[i]);
        nodes.add(mesh.solution.T[i]);
        nodes.add(mesh.report.nodalExact[i]);
        nodes.add(std::abs(mesh.solution.T[i] - mesh.report.nodalExact[i]));
    }
    const fs::path nodesPath = csvDir / (stem + "_nodes.csv");
    if (auto r = nodes.save(nodesPath); !r) {
        return r;
    }
    MetaJson nodeUnits;
    nodeUnits.set("x_m", "m").set("T_fe_C", "degC").set("T_exact_C", "degC").set("abs_err_C",
                                                                                  "degC");
    if (auto r = writeSidecar(ctx, nodesPath, "Nodal FE temperatures vs the exact solution", p,
                              nodeCols, nodeUnits, &details);
        !r) {
        return r;
    }

    const std::vector<std::string> midCols{"x_mid_m", "T_fe_mid_C", "T_exact_C", "abs_err_C"};
    CsvWriter mids(midCols);
    for (std::size_t e = 0; e < mesh.report.midX.size(); ++e) {
        mids.beginRow();
        mids.add(mesh.report.midX[e]);
        mids.add(mesh.report.midFe[e]);
        mids.add(mesh.report.midExact[e]);
        mids.add(std::abs(mesh.report.midFe[e] - mesh.report.midExact[e]));
    }
    const fs::path midsPath = csvDir / (stem + "_midpoints.csv");
    if (auto r = mids.save(midsPath); !r) {
        return r;
    }
    MetaJson midUnits;
    midUnits.set("x_mid_m", "m").set("T_fe_mid_C", "degC").set("T_exact_C", "degC").set(
        "abs_err_C", "degC");
    return writeSidecar(ctx, midsPath,
                        "Element-midpoint FE interpolation 0.5*(T_i + T_i+1) vs the exact "
                        "solution",
                        p, midCols, midUnits, &details);
}

Result<void> writeConvergenceCsv(const CommandContext& ctx, const RodProblem& p,
                                 const std::vector<StudyRow>& rows) {
    const std::vector<std::string> cols{"N",     "h_m",   "max_mid_err_C",   "expected_qh2_8k_C",
                                        "ratio", "order", "max_nodal_err_C", "energy_residual_W"};
    CsvWriter csv(cols);
    for (const StudyRow& r : rows) {
        csv.beginRow();
        csv.add(r.N);
        csv.add(r.h);
        csv.add(r.maxMidErr);
        csv.add(r.expected);
        // Ratio and order need a previous mesh: blank in the first row.
        if (std::isfinite(r.ratioToPrev)) {
            csv.add(r.ratioToPrev);
        } else {
            csv.addBlank();
        }
        if (std::isfinite(r.order)) {
            csv.add(r.order);
        } else {
            csv.addBlank();
        }
        csv.add(r.maxNodalErr);
        csv.add(r.energyResidual_W);
    }
    const fs::path path = ctx.outDir / "csv" / "convergence.csv";
    if (auto r = csv.save(path); !r) {
        return r;
    }
    MetaJson units;
    units.set("N", "elements")
        .set("h_m", "m")
        .set("max_mid_err_C", "degC")
        .set("expected_qh2_8k_C", "degC")
        .set("ratio", "1 (previous max_mid_err / this one; blank in row 1)")
        .set("order", "1 (log(ratio) / log(h_prev / h); blank in row 1)")
        .set("max_nodal_err_C", "degC")
        .set("energy_residual_W", "W (heat out left + right - q*A*L)");
    MetaJson details;
    details.set("mesh_sizes", kMeshSizes);
    return writeSidecar(ctx, path, "Midpoint-error convergence study", p, cols, units, &details);
}

Result<void> writePlots(const CommandContext& ctx, const RodProblem& p,
                        const std::vector<StudyRow>& rows, const std::vector<StudyMesh>& meshes) {
    const PlotMarker markers[] = {PlotMarker::Circle, PlotMarker::Square, PlotMarker::Triangle,
                                  PlotMarker::Diamond};
    PlotSpec tx;
    tx.title = "Rod temperature: FE solutions vs exact solution";
    tx.xLabel = "x (m)";
    tx.yLabel = "T (\xC2\xB0" "C)";
    // Nodal values are exact, so every mesh lies on the same curve. Draw the finest mesh first
    // so the sparser markers of the coarse meshes stay visible on top of it.
    for (std::size_t i = meshes.size(); i-- > 0;) {
        PlotSeries s;
        s.label = "FE, N = " + std::to_string(rows[i].N);
        s.x = meshes[i].solution.x;
        s.y = meshes[i].solution.T;
        s.color = plotcolor::kSeries[i % 4];
        s.marker = markers[i % 4];
        tx.series.push_back(std::move(s));
    }
    PlotSeries exact;
    exact.label = "Exact T(x)";
    exact.color = plotcolor::kReference;
    exact.dashed = true;
    for (int i = 0; i < kExactSamples; ++i) {
        const double x = p.L * i / (kExactSamples - 1);
        exact.x.push_back(x);
        exact.y.push_back(thermal::exactTemperature(p, x));
    }
    tx.series.push_back(std::move(exact));
    if (auto r = saveSvgPlot(tx, ctx.outDir / "plots" / "T_vs_x.svg"); !r) {
        return r;
    }

    PlotSpec err;
    err.title = "Max midpoint error vs element size (log-log)";
    err.xLabel = "h (m)";
    err.yLabel = "max |T_fe - T_exact| at midpoints (\xC2\xB0" "C)";
    err.logX = true;
    err.logY = true;
    PlotSeries measured;
    measured.label = "Measured (FE)";
    measured.color = plotcolor::kSeries[1];
    measured.marker = PlotMarker::Circle;
    PlotSeries reference;
    reference.label = "q\xC2\xB7h\xC2\xB2/(8k), slope 2";
    reference.color = plotcolor::kReference;
    reference.dashed = true;
    for (const StudyRow& r : rows) {
        measured.x.push_back(r.h);
        measured.y.push_back(r.maxMidErr);
        reference.x.push_back(r.h);
        reference.y.push_back(r.expected);
    }
    err.series = {measured, reference};
    return saveSvgPlot(err, ctx.outDir / "plots" / "error_vs_h_loglog.svg");
}

void printTable(const CommandContext& ctx, const std::vector<StudyRow>& rows) {
    ctx.out << "     N       h (m)   max mid err (C)   q*h^2/(8k) (C)    ratio    order"
               "   max nodal err (C)   energy resid (W)\n";
    for (const StudyRow& r : rows) {
        char line[256];
        std::snprintf(line, sizeof line, "%6d  %10.6f  %16.10f  %15.10f  %7s  %7s  %18.3e  %17.3e\n",
                      r.N, r.h, r.maxMidErr, r.expected,
                      std::isfinite(r.ratioToPrev) ? fmt("%.6f", r.ratioToPrev).c_str() : "-",
                      std::isfinite(r.order) ? fmt("%.6f", r.order).c_str() : "-",
                      r.maxNodalErr, r.energyResidual_W);
        ctx.out << line;
    }
}

// Prints one PASS/FAIL line per check; returns true when all pass.
bool runChecks(const CommandContext& ctx, const RodProblem& p, const std::vector<StudyRow>& rows,
               const std::vector<StudyMesh>& meshes) {
    bool all = true;
    const auto check = [&](bool pass, const std::string& what) {
        ctx.out << (pass ? "  PASS  " : "  FAIL  ") << what << "\n";
        all = all && pass;
    };
    bool bcExact = true;
    double nodal = 0.0;
    double midDev = 0.0;
    double energy = 0.0;
    double orderDev = 0.0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& T = meshes[i].solution.T;
        bcExact = bcExact && T.front() == p.T0 && T.back() == p.TL;
        nodal = std::max(nodal, rows[i].maxNodalErr);
        midDev = std::max(midDev, std::abs(rows[i].maxMidErr - rows[i].expected));
        energy = std::max(energy, std::abs(rows[i].energyResidual_W));
        if (i > 0) {
            orderDev = std::max(orderDev, std::abs(rows[i].order - 2.0));
        }
    }
    check(bcExact, "T[0] == T0 and T[N] == TL exactly on every mesh");
    check(nodal <= kNodalTol, "max nodal error " + fmt("%.3e", nodal) + " C <= 1e-10 C");
    check(midDev <= kMidpointTol,
          "max |midpoint error - q*h^2/(8k)| " + fmt("%.3e", midDev) + " C <= 1e-9 C");
    check(energy <= kEnergyTol, "max |energy residual| " + fmt("%.3e", energy) +
                                    " W <= 1e-10 W (q*A*L = " + fmt("%g", p.q * p.A * p.L) +
                                    " W)");
    check(orderDev <= kOrderTol, "observed order within 1e-6 of 2 (max deviation " +
                                     fmt("%.3e", orderDev) + ")");
    return all;
}

} // namespace

int runFea(const CommandContext& ctx) {
    const RodProblem problem; // the brief's rod
    ctx.out << "UniForge thermal FEA (Task C): steady 1D rod, two-node linear elements\n"
            << "  L = " << problem.L << " m, A = " << problem.A << " m^2, k = " << problem.k
            << " W/(m K), q = " << problem.q << " W/m^3, T0 = " << problem.T0
            << " C, TL = " << problem.TL << " C\n";

    std::vector<StudyMesh> meshes;
    auto rows = thermal::convergenceStudy(problem, kMeshSizes, {}, nullptr, &meshes);
    if (!rows) {
        ctx.err << "error: " << rows.error().message << "\n  hint: " << rows.error().hint << "\n";
        return kExitError;
    }

    const auto fail = [&](const Error& e) {
        ctx.err << "error: " << e.message << "\n  hint: " << e.hint << "\n";
        return kExitError;
    };
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        if (auto r = writeMeshCsvs(ctx, problem, kMeshSizes[i], meshes[i]); !r) {
            return fail(r.error());
        }
    }
    if (auto r = writeConvergenceCsv(ctx, problem, rows.value()); !r) {
        return fail(r.error());
    }
    if (auto r = writePlots(ctx, problem, rows.value(), meshes); !r) {
        return fail(r.error());
    }

    const thermal::TemperaturePeak peak = thermal::exactPeak(problem);
    ctx.out << "  exact peak T = " << fmt("%.6f", peak.T) << " C at x = " << fmt("%.6f", peak.x)
            << " m\n\n";
    printTable(ctx, rows.value());
    ctx.out << "\nChecks:\n";
    const bool pass = runChecks(ctx, problem, rows.value(), meshes);
    ctx.out << "\nWrote " << (ctx.outDir / "csv").generic_u8string() << "/fea_N*_{nodes,midpoints}"
            << ".csv, convergence.csv (+ .meta.json) and "
            << (ctx.outDir / "plots").generic_u8string()
            << "/{T_vs_x,error_vs_h_loglog}.svg\n";
    return pass ? kExitOk : kExitCheckFailed;
}

} // namespace uniforge::cli
