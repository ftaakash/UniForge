#include "core/solvers/amg/Amg.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <string>
#include <utility>

#include "core/solvers/Observers.hpp"
#include "core/sparse/SparseOps.hpp"

namespace uniforge::solvers {

namespace {

using Clock = std::chrono::steady_clock;
using sparse::Offset;
using sparse::toSize;

double secondsSince(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

std::string number(double v) {
    std::ostringstream s;
    s.precision(3);
    s << v;
    return s.str();
}

// Bytes read and written by one pass over a CSR matrix with vector in and out (SpMV or a sweep).
double passBytes(const CsrMatrix& A) {
    return 12.0 * static_cast<double>(A.nnz()) + 8.0 * static_cast<double>(A.rows + 1) +
           16.0 * static_cast<double>(std::max(A.rows, A.cols));
}

void gaussSeidel(const CsrMatrix& A, const Vector& b, Vector& x, bool forward) {
    const Index n = A.rows;
    for (Index step = 0; step < n; ++step) {
        const Index i = forward ? step : n - 1 - step;
        double s = b[toSize(i)];
        double d = 0.0;
        for (Offset q = A.rowPtr[toSize(i)]; q < A.rowPtr[toSize(i) + 1]; ++q) {
            const Index j = A.col[toSize(q)];
            if (j == i) {
                d = A.val[toSize(q)];
            } else {
                s -= A.val[toSize(q)] * x[toSize(j)];
            }
        }
        x[toSize(i)] = s / d;
    }
}

} // namespace

// ---- Building blocks --------------------------------------------------------------------------

std::vector<char> strongConnections(const CsrMatrix& A, double theta) {
    const sparse::Vector d = sparse::diagonal(A);
    std::vector<char> strong(toSize(A.nnz()), 0);
    for (Index i = 0; i < A.rows; ++i) {
        for (Offset q = A.rowPtr[toSize(i)]; q < A.rowPtr[toSize(i) + 1]; ++q) {
            const Index j = A.col[toSize(q)];
            if (j == i) {
                continue;
            }
            const double a = std::fabs(A.val[toSize(q)]);
            strong[toSize(q)] =
                a > 0.0 && a >= theta * std::sqrt(std::fabs(d[toSize(i)] * d[toSize(j)])) ? 1 : 0;
        }
    }
    return strong;
}

std::vector<Index> aggregate(const CsrMatrix& A, const std::vector<char>& strong, Index& count) {
    const Index n = A.rows;
    std::vector<Index> agg(toSize(n), -1);
    std::vector<char> isolated(toSize(n), 1);
    for (Index i = 0; i < n; ++i) {
        for (Offset q = A.rowPtr[toSize(i)]; q < A.rowPtr[toSize(i) + 1]; ++q) {
            if (strong[toSize(q)] != 0) {
                isolated[toSize(i)] = 0;
                break;
            }
        }
    }
    count = 0;
    // (a) Roots: every strong neighbour still free.
    for (Index i = 0; i < n; ++i) {
        if (isolated[toSize(i)] != 0 || agg[toSize(i)] >= 0) {
            continue;
        }
        bool allFree = true;
        for (Offset q = A.rowPtr[toSize(i)]; q < A.rowPtr[toSize(i) + 1] && allFree; ++q) {
            if (strong[toSize(q)] != 0 && agg[toSize(A.col[toSize(q)])] >= 0) {
                allFree = false;
            }
        }
        if (!allFree) {
            continue;
        }
        agg[toSize(i)] = count;
        for (Offset q = A.rowPtr[toSize(i)]; q < A.rowPtr[toSize(i) + 1]; ++q) {
            const Index j = A.col[toSize(q)];
            if (strong[toSize(q)] != 0 && isolated[toSize(j)] == 0) {
                agg[toSize(j)] = count;
            }
        }
        ++count;
    }
    // (b) Join the strongest neighbouring aggregate from (a).
    const std::vector<Index> afterRoots = agg;
    for (Index i = 0; i < n; ++i) {
        if (isolated[toSize(i)] != 0 || agg[toSize(i)] >= 0) {
            continue;
        }
        double best = 0.0;
        for (Offset q = A.rowPtr[toSize(i)]; q < A.rowPtr[toSize(i) + 1]; ++q) {
            const Index j = A.col[toSize(q)];
            if (strong[toSize(q)] != 0 && afterRoots[toSize(j)] >= 0 &&
                std::fabs(A.val[toSize(q)]) > best) {
                best = std::fabs(A.val[toSize(q)]);
                agg[toSize(i)] = afterRoots[toSize(j)];
            }
        }
    }
    // (c) Leftovers: new aggregates with their free strong neighbours.
    for (Index i = 0; i < n; ++i) {
        if (isolated[toSize(i)] != 0 || agg[toSize(i)] >= 0) {
            continue;
        }
        agg[toSize(i)] = count;
        for (Offset q = A.rowPtr[toSize(i)]; q < A.rowPtr[toSize(i) + 1]; ++q) {
            const Index j = A.col[toSize(q)];
            if (strong[toSize(q)] != 0 && agg[toSize(j)] < 0 && isolated[toSize(j)] == 0) {
                agg[toSize(j)] = count;
            }
        }
        ++count;
    }
    return agg;
}

CsrMatrix tentativeProlongator(const std::vector<Index>& aggregateOf, Index count) {
    CsrMatrix P;
    P.rows = static_cast<Index>(aggregateOf.size());
    P.cols = count;
    P.rowPtr.assign(aggregateOf.size() + 1, 0);
    for (std::size_t i = 0; i < aggregateOf.size(); ++i) {
        P.rowPtr[i + 1] = P.rowPtr[i] + (aggregateOf[i] >= 0 ? 1 : 0);
        if (aggregateOf[i] >= 0) {
            P.col.push_back(aggregateOf[i]);
            P.val.push_back(1.0);
        }
    }
    return P;
}

CsrMatrix smoothedProlongator(const CsrMatrix& A, const std::vector<char>& strong,
                              const CsrMatrix& P0) {
    // Filtered matrix A_F: strong couplings and the diagonal; weak couplings lumped onto it.
    CsrMatrix F;
    F.rows = A.rows;
    F.cols = A.cols;
    F.rowPtr.assign(toSize(A.rows) + 1, 0);
    std::vector<double> dF(toSize(A.rows), 0.0);
    for (Index i = 0; i < A.rows; ++i) {
        double diag = 0.0;
        double lumped = 0.0;
        for (Offset q = A.rowPtr[toSize(i)]; q < A.rowPtr[toSize(i) + 1]; ++q) {
            const Index j = A.col[toSize(q)];
            if (j == i) {
                diag = A.val[toSize(q)];
            } else if (strong[toSize(q)] != 0) {
                F.col.push_back(j);
                F.val.push_back(A.val[toSize(q)]);
            } else {
                lumped += A.val[toSize(q)];
            }
        }
        const double d = diag + lumped > 0.0 ? diag + lumped : diag; // keep a usable diagonal
        dF[toSize(i)] = d;
        // Insert the diagonal in sorted position.
        const auto rowBegin = F.col.begin() + F.rowPtr[toSize(i)];
        const auto pos = std::lower_bound(rowBegin, F.col.end(), i);
        const auto offset = pos - F.col.begin();
        F.col.insert(pos, i);
        F.val.insert(F.val.begin() + offset, d);
        F.rowPtr[toSize(i) + 1] = static_cast<Offset>(F.col.size());
    }
    // ρ(D_F⁻¹·A_F) ≤ max_i Σ_j |F_ij| / |d_i| (Gershgorin).
    double rho = 0.0;
    for (Index i = 0; i < F.rows; ++i) {
        double row = 0.0;
        for (Offset q = F.rowPtr[toSize(i)]; q < F.rowPtr[toSize(i) + 1]; ++q) {
            row += std::fabs(F.val[toSize(q)]);
        }
        rho = std::max(rho, row / std::fabs(dF[toSize(i)]));
    }
    const double omega = rho > 0.0 ? 4.0 / (3.0 * rho) : 0.0;

    auto FP = sparse::multiply(F, P0); // pattern ⊇ P0 (the diagonal of F is stored)
    CsrMatrix P = std::move(FP).value();
    for (Index i = 0; i < P.rows; ++i) {
        const double scale = omega / dF[toSize(i)];
        for (Offset q = P.rowPtr[toSize(i)]; q < P.rowPtr[toSize(i) + 1]; ++q) {
            P.val[toSize(q)] = -scale * P.val[toSize(q)];
        }
        for (Offset q = P0.rowPtr[toSize(i)]; q < P0.rowPtr[toSize(i) + 1]; ++q) {
            const Offset at = P.find(i, P0.col[toSize(q)]);
            P.val[toSize(at)] += P0.val[toSize(q)];
        }
    }
    return P;
}

// ---- Hierarchy --------------------------------------------------------------------------------

Result<SetupReport> AmgHierarchy::build(const CsrMatrix& K, sparse::KernelPool& pool,
                                        const SolverOptions& options) {
    if (K.rows != K.cols) {
        return makeError(ErrorCode::InvalidInput,
                         "AMG needs a square matrix, got " + std::to_string(K.rows) + " × " +
                             std::to_string(K.cols) + ".",
                         "This is a programming error in the caller.");
    }
    for (const double v : K.val) {
        if (!std::isfinite(v)) {
            return makeError(ErrorCode::NonFinite, "The matrix contains NaN or infinite entries.",
                             "Check the assembly inputs.");
        }
    }
    const auto t0 = Clock::now();
    pool_ = &pool;
    sweeps_ = std::max(1, options.amgSweeps);
    ops_.assign(1, &K);
    owned_.clear();
    levels_.clear();
    coarse_.reset();
    SetupReport report;

    const auto tc = Clock::now();
    while (ops_.back()->rows > options.amgCoarseSize &&
           static_cast<int>(ops_.size()) < options.amgMaxLevels) {
        const CsrMatrix& A = *ops_.back();
        const double theta =
            options.amgStrengthThreshold *
            std::pow(options.amgStrengthDecay, static_cast<double>(ops_.size() - 1));
        const std::vector<char> strong = strongConnections(A, theta);
        Index count = 0;
        Level lv;
        lv.aggregateOf = aggregate(A, strong, count);
        if (count == 0 || static_cast<double>(count) > 0.9 * static_cast<double>(A.rows)) {
            break; // coarsening stalled
        }
        lv.P = smoothedProlongator(A, strong, tentativeProlongator(lv.aggregateOf, count));
        lv.R = sparse::transpose(lv.P);
        auto AP = sparse::multiply(A, lv.P);
        if (!AP) {
            return std::move(AP).error();
        }
        auto Ac = sparse::multiply(lv.R, AP.value());
        if (!Ac) {
            return std::move(Ac).error();
        }
        lv.r.assign(toSize(A.rows), 0.0);
        lv.tmp.assign(toSize(A.rows), 0.0);
        lv.bc.assign(toSize(count), 0.0);
        lv.xc.assign(toSize(count), 0.0);
        // Pᵀ(A·P) is symmetric in exact arithmetic; rounding in the two products leaves tiny
        // asymmetries on entries that cancel. Averaging with the transpose (same pattern) makes
        // every coarse operator, and so the V-cycle, exactly symmetric (amendment A15).
        CsrMatrix coarse = std::move(Ac).value();
        const CsrMatrix coarseT = sparse::transpose(coarse);
        if (coarseT.col == coarse.col) {
            for (std::size_t q = 0; q < coarse.val.size(); ++q) {
                coarse.val[q] = 0.5 * (coarse.val[q] + coarseT.val[q]);
            }
        }
        owned_.push_back(std::make_unique<CsrMatrix>(std::move(coarse)));
        ops_.push_back(owned_.back().get());
        levels_.push_back(std::move(lv));
    }
    report.phases.emplace_back("coarsening", secondsSince(tc));

    const auto td = Clock::now();
    coarse_ = std::make_unique<DirectCholesky>(SolverKind::DirectRCM, Ordering::Rcm);
    auto cs = coarse_->setup(*ops_.back(), {}, pool, {});
    if (!cs) {
        return std::move(cs).error();
    }
    report.phases.emplace_back("coarse factorisation", secondsSince(td));
    if (cs.value().status != SolveStatus::Converged) {
        report.status = cs.value().status;
        report.detail = "Coarsest-level factorisation failed: " + cs.value().detail;
        report.seconds = secondsSince(t0);
        return report;
    }
    coarseSolution_.assign(toSize(ops_.back()->rows), 0.0);

    // Statistics and work per cycle.
    const double nnz0 = static_cast<double>(K.nnz());
    const double rows0 = static_cast<double>(K.rows);
    for (const CsrMatrix* A : ops_) {
        report.levels.emplace_back(A->rows, A->nnz());
        report.operatorComplexity += static_cast<double>(A->nnz()) / nnz0;
        report.gridComplexity += static_cast<double>(A->rows) / rows0;
    }
    bytesPerCycle_ = 0.0;
    for (std::size_t l = 0; l < levels_.size(); ++l) {
        bytesPerCycle_ += 2.0 * sweeps_ * passBytes(*ops_[l]) + passBytes(*ops_[l]) +
                          passBytes(levels_[l].R) + passBytes(levels_[l].P);
    }
    bytesPerCycle_ += 2.0 * 12.0 * static_cast<double>(coarse_->factor().val.size());
    report.footprint = footprint();
    report.seconds = secondsSince(t0);
    return report;
}

void AmgHierarchy::vcycle(const Vector& b, Vector& x) {
    cycle(0, b, x);
}

void AmgHierarchy::cycle(std::size_t level, const Vector& b, Vector& x) {
    if (level + 1 == ops_.size()) {
        auto r = coarse_->solve(b, coarseSolution_, {});
        (void)r; // sizes match by construction; the factor exists
        std::copy(coarseSolution_.begin(), coarseSolution_.end(), x.begin());
        return;
    }
    const CsrMatrix& A = *ops_[level];
    Level& lv = levels_[level];
    for (int s = 0; s < sweeps_; ++s) {
        gaussSeidel(A, b, x, true);
    }
    sparse::residual(*pool_, A, x, b, lv.r);
    sparse::spmv(*pool_, lv.R, lv.r, lv.bc);
    std::fill(lv.xc.begin(), lv.xc.end(), 0.0);
    cycle(level + 1, lv.bc, lv.xc);
    sparse::spmv(*pool_, lv.P, lv.xc, lv.tmp);
    sparse::axpy(*pool_, 1.0, lv.tmp, x);
    for (int s = 0; s < sweeps_; ++s) {
        gaussSeidel(A, b, x, false);
    }
}

MemoryFootprint AmgHierarchy::footprint() const {
    MemoryFootprint f;
    std::size_t ops = 0;
    for (const auto& m : owned_) {
        ops += m->bytes();
    }
    f.add("AMG coarse operators", ops);
    std::size_t transfer = 0;
    std::size_t work = 0;
    for (const Level& lv : levels_) {
        transfer += lv.P.bytes() + lv.R.bytes();
        work += (lv.r.capacity() + lv.tmp.capacity() + lv.bc.capacity() + lv.xc.capacity()) *
                    sizeof(double) +
                lv.aggregateOf.capacity() * sizeof(Index);
    }
    f.add("AMG prolongators and restrictions", transfer);
    f.add("AMG work vectors and aggregates", work);
    if (coarse_) {
        f.add("AMG coarse factor", coarse_->footprint().total());
    }
    return f;
}

// ---- AMG as preconditioner --------------------------------------------------------------------

SetupReport AmgPreconditioner::setup(const CsrMatrix& K, sparse::KernelPool& pool) {
    auto r = h_.build(K, pool, options_);
    if (!r) {
        SetupReport fail;
        fail.status = SolveStatus::Breakdown;
        fail.detail = r.error().message;
        return fail;
    }
    return r.value();
}

void AmgPreconditioner::apply(const Vector& r, Vector& z) {
    std::fill(z.begin(), z.end(), 0.0);
    h_.vcycle(r, z);
}

// ---- AMG as stand-alone solver ----------------------------------------------------------------

Result<SetupReport> AmgSolver::setup(const CsrMatrix& K, const CoordinateHint& /*hint*/,
                                     sparse::KernelPool& pool, const SolverOptions& options) {
    ready_ = false;
    auto r = h_.build(K, pool, options);
    if (!r) {
        return std::move(r).error();
    }
    SetupReport report = std::move(r).value();
    if (report.status != SolveStatus::Converged) {
        return report;
    }
    if (report.footprint.total() > options.memoryBudgetBytes) {
        report.status = SolveStatus::OverBudget;
        report.detail = "The AMG hierarchy needs " + std::to_string(report.footprint.total()) +
                        " bytes, more than the budget of " +
                        std::to_string(options.memoryBudgetBytes) + ".";
        return report;
    }
    k_ = &K;
    pool_ = &pool;
    r_.assign(toSize(K.rows), 0.0);
    ready_ = true;
    report.footprint = footprint();
    return report;
}

MemoryFootprint AmgSolver::footprint() const {
    MemoryFootprint f = h_.footprint();
    f.add("AMG solver residual", r_.capacity() * sizeof(double));
    return f;
}

Result<SolveReport> AmgSolver::solve(const Vector& f, Vector& x, const SolveControl& control) {
    if (!ready_) {
        return makeError(ErrorCode::InvalidInput, "AMG was not set up successfully.",
                         "Call setup() and check its status first.");
    }
    const CsrMatrix& K = *k_;
    sparse::KernelPool& pool = *pool_;
    const std::size_t n = toSize(K.rows);
    if (f.size() != n || x.size() != n ||
        (control.residualSumMask != nullptr && control.residualSumMask->size() != n)) {
        return makeError(ErrorCode::InvalidInput,
                         "Right-hand side, initial guess or mask does not match the " +
                             std::to_string(n) + " rows of K.",
                         "This is a programming error in the caller.");
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(f[i]) || !std::isfinite(x[i])) {
            return makeError(ErrorCode::NonFinite,
                             "The right-hand side or the initial guess contains NaN or infinity.",
                             "Check the load and boundary-condition inputs.");
        }
    }
    const double spmvBytes = passBytes(K);
    SolveReport rep;
    double observerSeconds = 0.0;
    const auto t0 = Clock::now();
    const auto solverSeconds = [&] { return secondsSince(t0) - observerSeconds; };
    double bytes = 0.0;

    const double normF = sparse::norm2(pool, f);
    if (normF == 0.0) {
        std::fill(x.begin(), x.end(), 0.0);
        rep.residualHistory.push_back(0.0);
    } else {
        sparse::residual(pool, K, x, f, r_);
        ++rep.spmvCount;
        bytes += spmvBytes;
        double rel = sparse::norm2(pool, r_) / normF;
        rep.residualHistory.push_back(rel);
        int growing = 0;
        double best = rel;
        int windowStart = 0;
        double windowBest = rel;
        int k = 0;
        while (rel > control.relTol) {
            if (k >= control.maxIterations) {
                rep.status = SolveStatus::MaxIterations;
                rep.detail = "Reached the cap of " + std::to_string(control.maxIterations) +
                             " cycles at relative residual " + number(rel) + ".";
                break;
            }
            ++k;
            h_.vcycle(f, x);
            sparse::residual(pool, K, x, f, r_);
            ++rep.spmvCount;
            bytes += h_.bytesPerCycle() + spmvBytes;
            const double next = sparse::norm2(pool, r_) / normF;
            rep.residualHistory.push_back(next);
            if (control.observer != nullptr) {
                const auto o0 = Clock::now();
                control.observer->onIteration(k, next, x);
                observerSeconds += secondsSince(o0);
            }
            rep.iterTimestamps.push_back(solverSeconds());
            if (!std::isfinite(next)) {
                rep.status = SolveStatus::Breakdown;
                rep.detail =
                    "The residual became NaN or infinite at cycle " + std::to_string(k) + ".";
                break;
            }
            growing = next > rel ? growing + 1 : 0;
            rel = next;
            if (rel <= control.relTol) {
                break;
            }
            best = std::min(best, rel);
            // Real divergence leaves the best residual far behind; growth near the rounding floor
            // (seen on HS-7 at ~3e-12) does not.
            if (growing >= 3 && rel > 10.0 * best) {
                rep.status = SolveStatus::Diverging;
                rep.detail = "The residual grew for three cycles in a row to more than 10× its "
                             "best (relative residual " +
                             number(rel) + " at cycle " + std::to_string(k) + ").";
                break;
            }
            if (rel < windowBest / 10.0) {
                windowBest = rel;
                windowStart = k;
            } else if (control.stagnationWindow > 0 &&
                       k - windowStart >= control.stagnationWindow) {
                rep.status = SolveStatus::Stagnated;
                rep.detail = "The residual did not drop 10× within " +
                             std::to_string(control.stagnationWindow) + " cycles.";
                break;
            }
            if (rep.iterTimestamps.back() > control.wallTimeCap) {
                rep.status = SolveStatus::TimeCap;
                rep.detail = "Reached the wall-time cap of " + number(control.wallTimeCap) +
                             " s after " + std::to_string(k) + " cycles.";
                break;
            }
            if (control.cancel != nullptr && control.cancel->cancelled()) {
                rep.status = SolveStatus::Cancelled;
                rep.detail = "Cancelled after " + std::to_string(k) + " cycles.";
                break;
            }
        }
        rep.iterations = k;
    }
    rep.seconds = solverSeconds();
    rep.observerSeconds = observerSeconds;
    rep.bytesMoved = bytes;
    rep.workSpmvEq = spmvBytes > 0.0 ? bytes / spmvBytes : 0.0;

    sparse::residual(pool, K, x, f, r_);
    rep.finalTrueRelResidual = normF > 0.0 ? sparse::norm2(pool, r_) / normF : 0.0;
    if (control.residualSumMask != nullptr) {
        double s = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            if ((*control.residualSumMask)[i] != 0) {
                s += r_[i];
            }
        }
        rep.residualSum = s;
    } else {
        rep.residualSum = sparse::sum(pool, r_);
    }
    return rep;
}

} // namespace uniforge::solvers
