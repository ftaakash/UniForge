#include "core/study/Runner.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>

#include "core/common/AtomicFile.hpp"
#include "core/common/CsvWriter.hpp"
#include "core/common/MetaJson.hpp"
#include "core/fem3d/Assembly.hpp"
#include "core/fem3d/Post.hpp"
#include "core/solvers/LinearSolver.hpp"
#include "core/solvers/Observers.hpp"
#include "core/solvers/amg/Amg.hpp"
#include "core/solvers/direct/Cholesky.hpp"
#include "core/solvers/krylov/Ic0.hpp"
#include "core/solvers/krylov/Pcg.hpp"

namespace uniforge::study {

namespace {

using Clock = std::chrono::steady_clock;
using solvers::SolverKind;
using solvers::SolveStatus;

double secondsSince(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

std::string safeText(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (c == ',') {
            out += ';';
        } else if (c == '"' || c == '\n' || c == '\r') {
            out += ' ';
        } else if (u >= 0x80) {
            if ((u & 0xC0) != 0x80) {
                out += '?'; // one '?' per non-ASCII character (skip continuation bytes)
            }
        } else {
            out += c;
        }
    }
    return out;
}

std::string roleName(RunRole r) {
    switch (r) {
    case RunRole::Solve:
        return "solve";
    case RunRole::Reference:
        return "reference";
    case RunRole::Verification:
        return "verification";
    }
    return "?";
}

std::unique_ptr<solvers::LinearOperatorSolver> makeRunSolver(const RunSpec& spec) {
    if (spec.naturalOrdering) {
        return std::make_unique<solvers::DirectCholesky>(spec.solver, solvers::Ordering::Natural);
    }
    if (spec.ic0NoShift) {
        return std::make_unique<solvers::Pcg>(
            SolverKind::PcgIc0,
            std::make_unique<solvers::Ic0Preconditioner>(std::vector<double>{0.0}));
    }
    auto s = solvers::makeSolver(spec.solver);
    return s ? std::move(s).value() : nullptr;
}

double median(std::vector<double> v) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    const std::size_t m = v.size() / 2;
    return v.size() % 2 == 1 ? v[m] : 0.5 * (v[m - 1] + v[m]);
}

std::string phasesText(const solvers::SetupReport& r) {
    std::string s;
    for (const auto& [name, sec] : r.phases) {
        s += (s.empty() ? "" : "; ") + name + "=" + formatShortest(sec) + "s";
    }
    return s;
}

// ---- E1 verification checks (same cases as tests/numerical/fem3d) ---------------------------

struct Check {
    double fe = 0.0;
    double reference = 0.0;
    double error = 0.0;
    std::string detail;
    long long dof = 0;
    int iterations = 0;
};

Result<Check> solvePcg(const fem3d::Design& d, double s, const fem3d::AssemblyOptions& o,
                       fem3d::HexMesh& mesh, fem3d::LinearSystem& sys, solvers::Vector& T,
                       int& iterations) {
    fem3d::MeshSpec spec;
    spec.targetSpacing = s;
    auto m = fem3d::buildMesh(d, spec);
    if (!m) {
        return std::move(m).error();
    }
    mesh = std::move(m).value();
    auto a = fem3d::assemble(d, mesh, o);
    if (!a) {
        return std::move(a).error();
    }
    sys = std::move(a).value();
    sparse::KernelPool pool(1);
    solvers::Pcg pcg(SolverKind::PcgJacobi, std::make_unique<solvers::JacobiPreconditioner>());
    auto su = pcg.setup(sys.K, {}, pool, {});
    if (!su) {
        return std::move(su).error();
    }
    solvers::SolveControl c;
    c.relTol = 1e-13;
    c.maxIterations = 200000;
    c.stagnationWindow = 0;
    T.assign(sys.f.size(), 0.0);
    auto r = pcg.solve(sys.f, T, c);
    if (!r) {
        return std::move(r).error();
    }
    iterations = r.value().iterations;
    return Check{};
}

fem3d::Design boxDesign(double L, double k, const fem3d::FaceBc& bc) {
    fem3d::Design d;
    d.name = "box";
    d.materials = {{"m", k, 0.0}};
    fem3d::Box b;
    b.name = "box";
    b.min = {0.0, 0.0, 0.0};
    b.max = {L, L, L};
    b.faces.fill(bc);
    d.boxes = {b};
    return d;
}

Result<Check> runCheck(const std::string& check) {
    fem3d::HexMesh mesh;
    fem3d::LinearSystem sys;
    solvers::Vector T;
    int it = 0;
    Check out;
    if (check == "bar") {
        const auto exact = [](double x, double, double) {
            return 100.0 + 420.0 * x - 500.0 * x * x;
        };
        auto r = solvePcg(fem3d::makeV1Bar(), 0.005, {}, mesh, sys, T, it);
        if (!r) {
            return r;
        }
        const auto e = fem3d::errorNorms(mesh, T, exact);
        const auto p = fem3d::peakTemperature(mesh, T);
        out.fe = p.t;
        out.reference = 188.2;
        out.error = e.maxNodal;
        out.detail = "peak at x = " + formatShortest(p.at[0]) + " m; max nodal error " +
                     formatShortest(e.maxNodal) + " K";
    } else if (check.rfind("mms", 0) == 0) {
        const int cells = std::stoi(check.substr(3));
        const double L = 0.020, k = 200.0, h = 1000.0, pi = std::acos(-1.0);
        const double a = 0.7 * pi / L, b = 0.5 * pi / L, c = 0.3 * pi / L;
        const auto exact = [=](double x, double y, double z) {
            return 300.0 + 20.0 * std::cos(a * x) * std::cos(b * y) * std::cos(c * z);
        };
        fem3d::AssemblyOptions o;
        o.source = [=](double x, double y, double z) {
            return k * (a * a + b * b + c * c) * (exact(x, y, z) - 300.0);
        };
        o.ambient = [=](double x, double y, double z) {
            const double gx = -20.0 * a * std::sin(a * x) * std::cos(b * y) * std::cos(c * z);
            const double gy = -20.0 * b * std::cos(a * x) * std::sin(b * y) * std::cos(c * z);
            const double gz = -20.0 * c * std::cos(a * x) * std::cos(b * y) * std::sin(c * z);
            double dn = gz;
            if (x <= 1e-12) {
                dn = -gx;
            } else if (x >= L - 1e-12) {
                dn = gx;
            } else if (y <= 1e-12) {
                dn = -gy;
            } else if (y >= L - 1e-12) {
                dn = gy;
            } else if (z <= 1e-12) {
                dn = -gz;
            }
            return exact(x, y, z) + (k / h) * dn;
        };
        auto r = solvePcg(boxDesign(L, k, fem3d::FaceBc::convection(h, 0.0)), L / cells, o, mesh,
                          sys, T, it);
        if (!r) {
            return r;
        }
        const auto e = fem3d::errorNorms(mesh, T, exact);
        out.fe = e.l2 / std::sqrt(e.volume); // RMS error, K
        out.reference = 0.0;
        out.error = e.maxNodal;
        out.detail = std::to_string(cells) + " cells per edge; RMS and max nodal error in K";
    } else if (check == "fin") {
        const double t = 0.002, W = 0.040, Lf = 0.026, k = 200.0, h = 25.0, tInf = 25.0, tb = 80.0;
        fem3d::Design d = boxDesign(1.0, k, fem3d::FaceBc::convection(h, tInf));
        d.boxes[0].max = {t, W, Lf};
        d.boxes[0].faces[fem3d::ZMin] = fem3d::FaceBc::dirichlet(tb);
        auto r = solvePcg(d, 1e-3, {}, mesh, sys, T, it);
        if (!r) {
            return r;
        }
        const double P = 2.0 * (W + t), A = W * t;
        const double m = std::sqrt(h * P / (k * A)), ratio = h / (m * k);
        const double q = std::sqrt(h * P * k * A) * (tb - tInf) *
                         (std::sinh(m * Lf) + ratio * std::cosh(m * Lf)) /
                         (std::cosh(m * Lf) + ratio * std::sinh(m * Lf));
        const auto audit = fem3d::energyAudit(d, mesh, sys, T);
        out.fe = audit.heatConvected;
        out.reference = q;
        out.error = audit.heatConvected / q - 1.0;
        out.detail = "fin heat in W vs 1D fin theory with convective tip (Bergman et al. 2011)";
    } else {
        return makeError(ErrorCode::InvalidInput, "Unknown verification check '" + check + "'.");
    }
    out.dof = static_cast<long long>(T.size());
    out.iterations = it;
    return out;
}

} // namespace

// ---- RunRecord --------------------------------------------------------------------------------

const std::vector<std::string>& resultColumns() {
    static const std::vector<std::string> columns{"id",
                                                  "experiment",
                                                  "role",
                                                  "check",
                                                  "design",
                                                  "system",
                                                  "spacing_mm",
                                                  "solver",
                                                  "threads",
                                                  "observer",
                                                  "dof",
                                                  "nnz",
                                                  "cells",
                                                  "max_aspect",
                                                  "assembly_s",
                                                  "matrix_bytes",
                                                  "setup_status",
                                                  "setup_s",
                                                  "setup_phases",
                                                  "factor_nnz",
                                                  "predicted_factor_nnz",
                                                  "factor_flops",
                                                  "ic0_shift",
                                                  "ic0_retries",
                                                  "amg_levels",
                                                  "op_complexity",
                                                  "solve_status",
                                                  "reps",
                                                  "solve_s_median",
                                                  "solve_s_min",
                                                  "solve_s_max",
                                                  "total_s_median",
                                                  "iterations",
                                                  "spmv_eq",
                                                  "kappa_est",
                                                  "final_rel_residual",
                                                  "residual_sum_W",
                                                  "observer_s",
                                                  "err_vs_ref_K",
                                                  "ref_source",
                                                  "T_max_C",
                                                  "R_th_K_per_W",
                                                  "heat_in_W",
                                                  "heat_convected_W",
                                                  "heat_dirichlet_W",
                                                  "rel_imbalance",
                                                  "solver_bytes",
                                                  "peak_ws_bytes",
                                                  "core",
                                                  "cpu_load_pct",
                                                  "git_commit",
                                                  "started_utc",
                                                  "check_fe",
                                                  "check_reference",
                                                  "check_error",
                                                  "detail"};
    return columns;
}

void RunRecord::set(const std::string& column, const std::string& value) {
    for (auto& [k, v] : cells_) {
        if (k == column) {
            v = value;
            return;
        }
    }
    cells_.emplace_back(column, value);
}

void RunRecord::set(const std::string& column, double value) {
    set(column, std::isfinite(value) ? formatShortest(value) : std::string());
}

void RunRecord::set(const std::string& column, long long value) {
    set(column, std::to_string(value));
}

std::string RunRecord::get(const std::string& column) const {
    for (const auto& [k, v] : cells_) {
        if (k == column) {
            return v;
        }
    }
    return {};
}

std::string RunRecord::toCsv() const {
    std::string header;
    std::string row;
    bool first = true;
    for (const std::string& c : resultColumns()) {
        if (!first) {
            header += ',';
            row += ',';
        }
        first = false;
        header += c;
        row += safeText(get(c));
    }
    return header + "\n" + row + "\n";
}

Result<RunRecord> RunRecord::fromCsv(const std::string& text) {
    std::istringstream in(text);
    std::string header;
    std::string row;
    if (!std::getline(in, header) || !std::getline(in, row)) {
        return makeError(ErrorCode::ParseError, "A result file needs a header and one row.");
    }
    const auto split = [](const std::string& line) {
        std::vector<std::string> out;
        std::string cell;
        for (const char c : line) {
            if (c == ',') {
                out.push_back(cell);
                cell.clear();
            } else if (c != '\r') {
                cell += c;
            }
        }
        out.push_back(cell);
        return out;
    };
    const auto keys = split(header);
    const auto values = split(row);
    if (keys.size() != values.size()) {
        return makeError(ErrorCode::ParseError, "A result file has " + std::to_string(keys.size()) +
                                                    " columns but " +
                                                    std::to_string(values.size()) + " values.");
    }
    RunRecord r;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        r.set(keys[i], values[i]);
    }
    return r;
}

// ---- References -------------------------------------------------------------------------------

Result<void> saveReference(const std::filesystem::path& file, const std::vector<double>& T) {
    std::string bytes = "UFREF1\n";
    const std::uint64_t n = T.size();
    bytes.append(reinterpret_cast<const char*>(&n), sizeof n);
    bytes.append(reinterpret_cast<const char*>(T.data()), T.size() * sizeof(double));
    return writeFileAtomic(file, bytes);
}

Result<std::vector<double>> loadReference(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return makeError(ErrorCode::IoError, "Cannot open reference " + file.string() + ".");
    }
    std::string magic(7, '\0');
    in.read(magic.data(), 7);
    std::uint64_t n = 0;
    in.read(reinterpret_cast<char*>(&n), sizeof n);
    if (!in || magic != "UFREF1\n" || n > (std::uint64_t(1) << 32)) {
        return makeError(ErrorCode::ParseError, "Reference " + file.string() + " is not valid.");
    }
    std::vector<double> T(static_cast<std::size_t>(n));
    in.read(reinterpret_cast<char*>(T.data()), static_cast<std::streamsize>(n * sizeof(double)));
    if (!in) {
        return makeError(ErrorCode::ParseError, "Reference " + file.string() + " is truncated.");
    }
    return T;
}

// ---- One run ------------------------------------------------------------------------------------

Result<RunRecord> executeRun(const RunSpec& spec, const RunPaths& paths,
                             const std::string& gitCommit) {
    RunRecord rec;
    rec.set("id", spec.id);
    rec.set("experiment", spec.experiment);
    rec.set("role", roleName(spec.role));
    rec.set("check", spec.check);
    rec.set("git_commit", gitCommit);
    rec.set("started_utc", formatUtcIso8601(std::chrono::system_clock::now()));
    rec.set("threads", static_cast<long long>(spec.threads));
    rec.set("observer", std::string(spec.observer ? "1" : "0"));

    if (spec.role == RunRole::Verification) {
        const auto t0 = Clock::now();
        auto c = runCheck(spec.check);
        if (!c) {
            return std::move(c).error();
        }
        rec.set("solver", std::string("PCG-J"));
        rec.set("dof", c.value().dof);
        rec.set("iterations", static_cast<long long>(c.value().iterations));
        rec.set("total_s_median", secondsSince(t0));
        rec.set("check_fe", c.value().fe);
        rec.set("check_reference", c.value().reference);
        rec.set("check_error", c.value().error);
        rec.set("detail", c.value().detail);
        rec.set("solve_status", std::string("Converged"));
        return rec;
    }

    const fem3d::Design design = makeDesign(spec);
    rec.set("design", designName(spec.design));
    rec.set("system", systemKey(spec));
    rec.set("spacing_mm", spec.spacing * 1e3);
    rec.set("solver",
            spec.role == RunRole::Reference
                ? std::string("reference")
                : std::string(spec.naturalOrdering ? "DIRECT-NAT" : solvers::label(spec.solver)));

    fem3d::MeshSpec ms;
    ms.targetSpacing = spec.spacing;
    ms.fixedLayerCells = spec.timCells;
    auto mesh = fem3d::buildMesh(design, ms);
    if (!mesh) {
        return std::move(mesh).error();
    }
    auto assembled = fem3d::assemble(design, mesh.value());
    if (!assembled) {
        return std::move(assembled).error();
    }
    const fem3d::HexMesh& m = mesh.value();
    const fem3d::LinearSystem& sys = assembled.value();
    rec.set("dof", static_cast<long long>(sys.f.size()));
    rec.set("nnz", static_cast<long long>(sys.K.nnz()));
    rec.set("cells", static_cast<long long>(m.stats.activeCells));
    rec.set("max_aspect", m.stats.maxAspect);
    rec.set("assembly_s", sys.assemblySeconds);
    rec.set("matrix_bytes", static_cast<long long>(sys.matrixBytes));

    sparse::KernelPool pool(spec.threads);
    const auto refFile = paths.referenceFile(systemKey(spec));

    if (spec.role == RunRole::Reference) {
        if (std::filesystem::exists(refFile)) {
            auto existing = loadReference(refFile);
            if (existing && existing.value().size() == sys.f.size()) {
                rec.set("ref_source", std::string("reused"));
                rec.set("solve_status", std::string("Converged"));
                return rec;
            }
        }
        const auto t0 = Clock::now();
        solvers::SolverOptions opt;
        opt.memoryBudgetBytes = spec.memoryBudget;
        solvers::Vector T(sys.f.size(), 0.0);
        std::string source = "DIRECT-ND";
        solvers::DirectCholesky direct(SolverKind::DirectND, solvers::Ordering::NestedDissection);
        auto su = direct.setup(sys.K, {}, pool, opt);
        if (!su) {
            return std::move(su).error();
        }
        solvers::SolveControl c;
        c.residualSumMask = &sys.freeMask;
        solvers::SolveReport report;
        if (su.value().status == SolveStatus::Converged) {
            auto r = direct.solve(sys.f, T, c);
            if (!r) {
                return std::move(r).error();
            }
            report = r.value();
        } else {
            source = "AMG-PCG 1e-12";
            solvers::Pcg pcg(SolverKind::AmgPcg, std::make_unique<solvers::AmgPreconditioner>());
            auto s2 = pcg.setup(sys.K, {}, pool, opt);
            if (!s2) {
                return std::move(s2).error();
            }
            c.relTol = 1e-12;
            c.maxIterations = 20000;
            c.stagnationWindow = 0;
            auto r = pcg.solve(sys.f, T, c);
            if (!r) {
                return std::move(r).error();
            }
            report = r.value();
        }
        rec.set("ref_source", source);
        rec.set("solve_status", std::string(solvers::label(report.status)));
        rec.set("final_rel_residual", report.finalTrueRelResidual);
        rec.set("total_s_median", secondsSince(t0));
        if (report.status == SolveStatus::Converged) {
            if (auto saved = saveReference(refFile, T); !saved) {
                return std::move(saved).error();
            }
        }
        return rec;
    }

    // ---- Solve run ----
    std::vector<double> reference;
    if (spec.observer && std::filesystem::exists(refFile)) {
        auto loaded = loadReference(refFile);
        if (loaded && loaded.value().size() == sys.f.size()) {
            reference = std::move(loaded).value();
        }
    }
    solvers::SolverOptions opt;
    opt.memoryBudgetBytes = spec.memoryBudget;
    solvers::SolveControl control;
    control.relTol = spec.relTol;
    control.maxIterations = spec.maxIterations;
    control.wallTimeCap = spec.wallTimeCap;
    control.residualSumMask = &sys.freeMask;

    std::vector<double> setupTimes, solveTimes, totalTimes;
    solvers::SetupReport firstSetup;
    solvers::SolveReport firstSolve;
    solvers::Vector T(sys.f.size(), 0.0);
    std::size_t footprint = 0;
    int reps = 1;
    for (int rep = 0; rep < reps; ++rep) {
        auto solver = makeRunSolver(spec);
        if (!solver) {
            return makeError(ErrorCode::InvalidInput,
                             "Cannot create the solver of " + spec.id + ".");
        }
        const solvers::CoordinateHint hint{&m.nodeXyz, &m.nodeGrid}; // geometric multigrid
        auto su = solver->setup(sys.K, hint, pool, opt);
        if (!su) {
            return std::move(su).error();
        }
        if (rep == 0) {
            firstSetup = su.value();
        }
        if (su.value().status != SolveStatus::Converged) {
            setupTimes.push_back(su.value().seconds);
            break; // a failed setup is the result; no repetitions
        }
        footprint = solver->footprint().total();
        std::fill(T.begin(), T.end(), 0.0);
        std::unique_ptr<solvers::ErrorVsReference> observer;
        solvers::SolveControl c = control;
        if (rep == 0 && !reference.empty()) {
            observer = std::make_unique<solvers::ErrorVsReference>(reference, pool);
            c.observer = observer.get();
        }
        auto so = solver->solve(sys.f, T, c);
        if (!so) {
            return std::move(so).error();
        }
        setupTimes.push_back(su.value().seconds);
        solveTimes.push_back(so.value().seconds);
        totalTimes.push_back(su.value().seconds + so.value().seconds);
        if (rep == 0) {
            firstSolve = so.value();
            // History of the first repetition: iteration, solver time, residual, error.
            std::string h = "iteration,time_s,rel_residual,error_K\n";
            const auto& hist = firstSolve.residualHistory;
            for (std::size_t k = 0; k < hist.size(); ++k) {
                const double t = k == 0 ? 0.0
                                 : k - 1 < firstSolve.iterTimestamps.size()
                                     ? firstSolve.iterTimestamps[k - 1]
                                     : firstSolve.seconds;
                std::string err;
                if (observer && k >= 1 && k - 1 < observer->errors().size()) {
                    err = formatShortest(observer->errors()[k - 1]);
                }
                h += std::to_string(k) + "," + formatShortest(t) + "," + formatShortest(hist[k]) +
                     "," + err + "\n";
            }
            if (auto w = writeFileAtomic(paths.historyFile(spec.id), h); !w) {
                return std::move(w).error();
            }
            // Repetition policy (amendment A6): by the first repetition's total time.
            const double t1 = totalTimes.back();
            reps = t1 < 10.0 ? 5 : (t1 < 60.0 ? 3 : 1);
            if (so.value().status != SolveStatus::Converged) {
                reps = 1; // a capped or failed solve is not repeated
            }
        }
    }

    rec.set("setup_status", std::string(solvers::label(firstSetup.status)));
    rec.set("setup_s", median(setupTimes));
    rec.set("setup_phases", phasesText(firstSetup));
    rec.set("factor_nnz", static_cast<long long>(firstSetup.factorNnz));
    rec.set("predicted_factor_nnz", static_cast<long long>(firstSetup.predictedFactorNnz));
    rec.set("factor_flops", firstSetup.factorFlops);
    rec.set("ic0_shift", firstSetup.preconditionerShift);
    rec.set("ic0_retries", static_cast<long long>(firstSetup.preconditionerRetries));
    rec.set("amg_levels", static_cast<long long>(firstSetup.levels.size()));
    rec.set("op_complexity", firstSetup.operatorComplexity);
    rec.set("solver_bytes", static_cast<long long>(footprint));
    std::string detail = firstSetup.detail;
    if (firstSetup.status != SolveStatus::Converged) {
        rec.set("solve_status", std::string("not run"));
        rec.set("reps", 0LL);
        rec.set("detail", detail);
        return rec;
    }
    rec.set("solve_status", std::string(solvers::label(firstSolve.status)));
    rec.set("reps", static_cast<long long>(solveTimes.size()));
    rec.set("solve_s_median", median(solveTimes));
    rec.set("solve_s_min", *std::min_element(solveTimes.begin(), solveTimes.end()));
    rec.set("solve_s_max", *std::max_element(solveTimes.begin(), solveTimes.end()));
    rec.set("total_s_median", median(totalTimes));
    rec.set("iterations", static_cast<long long>(firstSolve.iterations));
    rec.set("spmv_eq", firstSolve.workSpmvEq);
    rec.set("kappa_est", firstSolve.kappaEstimate);
    rec.set("final_rel_residual", firstSolve.finalTrueRelResidual);
    rec.set("residual_sum_W", firstSolve.residualSum);
    rec.set("observer_s", firstSolve.observerSeconds);
    if (!firstSolve.detail.empty()) {
        detail += (detail.empty() ? "" : " | ") + firstSolve.detail;
    }
    rec.set("detail", detail);
    if (!reference.empty()) {
        rec.set("err_vs_ref_K", sparse::maxAbsDiff(pool, T, reference));
        rec.set("ref_source", std::string("refs"));
    }
    const fem3d::Peak peak = fem3d::peakTemperature(m, T);
    const fem3d::EnergyAudit audit = fem3d::energyAudit(design, m, sys, T);
    rec.set("T_max_C", peak.t);
    const double power = design.heatGenerated();
    const bool convective = spec.design != DesignKey::V1Bar && spec.design != DesignKey::Cube;
    if (convective && power > 0.0 && !spec.singular) {
        rec.set("R_th_K_per_W", (peak.t - 25.0) / power);
    }
    rec.set("heat_in_W", audit.heatIn);
    rec.set("heat_convected_W", audit.heatConvected);
    rec.set("heat_dirichlet_W", audit.heatOutDirichlet);
    rec.set("rel_imbalance", audit.relImbalance);
    return rec;
}

} // namespace uniforge::study
