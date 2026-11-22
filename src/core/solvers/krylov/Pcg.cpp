#include "core/solvers/krylov/Pcg.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <string>
#include <utility>

#include "core/solvers/Observers.hpp"
#include "core/solvers/krylov/Lanczos.hpp"

namespace uniforge::solvers {

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

bool allFinite(const Vector& v) {
    for (const double x : v) {
        if (!std::isfinite(x)) {
            return false;
        }
    }
    return true;
}

std::string number(double v) {
    std::ostringstream s;
    s.precision(3);
    s << v;
    return s.str();
}

} // namespace

Pcg::Pcg(SolverKind kind, std::unique_ptr<Preconditioner> preconditioner)
    : kind_(kind)
    , m_(std::move(preconditioner)) {}

Result<SetupReport> Pcg::setup(const CsrMatrix& K, const CoordinateHint& /*hint*/,
                               sparse::KernelPool& pool, const SolverOptions& options) {
    ready_ = false;
    if (K.rows != K.cols) {
        return makeError(ErrorCode::InvalidInput,
                         "PCG needs a square matrix, got " + std::to_string(K.rows) + " × " +
                             std::to_string(K.cols) + ".",
                         "This is a programming error in the caller.");
    }
    if (!allFinite(K.val)) {
        return makeError(ErrorCode::NonFinite, "The matrix contains NaN or infinite entries.",
                         "Check the assembly inputs.");
    }
    const auto t0 = Clock::now();
    // Work vectors and the preconditioner are O(n); check the budget before allocating them.
    const std::size_t n = sparse::toSize(K.rows);
    const std::size_t workBytes = 4 * n * sizeof(double);
    SetupReport report;
    if (workBytes > options.memoryBudgetBytes) {
        report.status = SolveStatus::OverBudget;
        report.detail = "PCG work vectors need " + std::to_string(workBytes) +
                        " bytes, more than the budget of " +
                        std::to_string(options.memoryBudgetBytes) + ".";
        report.seconds = secondsSince(t0);
        return report;
    }
    const auto tp = Clock::now();
    SetupReport pre = m_->setup(K, pool);
    if (pre.status != SolveStatus::Converged) {
        pre.seconds = secondsSince(t0);
        return pre;
    }
    // Keep everything the preconditioner reported (IC(0) shift, AMG hierarchy, its phases).
    report = std::move(pre);
    report.phases.emplace_back("preconditioner", secondsSince(tp));
    r_.assign(n, 0.0);
    z_.assign(n, 0.0);
    p_.assign(n, 0.0);
    q_.assign(n, 0.0);
    k_ = &K;
    pool_ = &pool;
    report.footprint = footprint();
    if (report.footprint.total() > options.memoryBudgetBytes) {
        report.status = SolveStatus::OverBudget;
        report.detail = "PCG with " + std::string(m_->name()) + " needs " +
                        std::to_string(report.footprint.total()) +
                        " bytes, more than the budget of " +
                        std::to_string(options.memoryBudgetBytes) + ".";
        r_ = z_ = p_ = q_ = Vector{};
        report.seconds = secondsSince(t0);
        return report;
    }
    ready_ = true;
    report.seconds = secondsSince(t0);
    return report;
}

MemoryFootprint Pcg::footprint() const {
    MemoryFootprint f = m_->footprint();
    f.add("PCG work vectors (r, z, p, q)",
          (r_.capacity() + z_.capacity() + p_.capacity() + q_.capacity()) * sizeof(double));
    return f;
}

Result<SolveReport> Pcg::solve(const Vector& f, Vector& x, const SolveControl& control) {
    if (!ready_) {
        return makeError(ErrorCode::InvalidInput, "PCG was not set up successfully.",
                         "Call setup() and check its status first.");
    }
    const CsrMatrix& K = *k_;
    sparse::KernelPool& pool = *pool_;
    const std::size_t n = sparse::toSize(K.rows);
    if (f.size() != n || x.size() != n ||
        (control.residualSumMask != nullptr && control.residualSumMask->size() != n)) {
        return makeError(ErrorCode::InvalidInput,
                         "Right-hand side, initial guess or mask does not match the " +
                             std::to_string(n) + " rows of K.",
                         "This is a programming error in the caller.");
    }
    if (!allFinite(f) || !allFinite(x)) {
        return makeError(ErrorCode::NonFinite,
                         "The right-hand side or the initial guess contains NaN or infinity.",
                         "Check the load and boundary-condition inputs.");
    }

    // Work accounting (amendment A9): bytes each kernel reads and writes.
    const double dn = static_cast<double>(n);
    const double spmvBytes = 12.0 * static_cast<double>(K.nnz()) + 8.0 * (dn + 1.0) + 16.0 * dn;
    const double precBytes = m_->bytesPerApply();

    SolveReport rep;
    double observerSeconds = 0.0;
    const auto t0 = Clock::now();
    const auto solverSeconds = [&] { return secondsSince(t0) - observerSeconds; };

    double bytes = 0.0;
    std::vector<double> alpha;
    std::vector<double> beta;

    const double normF = sparse::norm2(pool, f);
    bytes += 8.0 * dn;
    if (normF == 0.0) {
        std::fill(x.begin(), x.end(), 0.0); // K·x = 0 has the solution x = 0 for SPD K
        rep.residualHistory.push_back(0.0);
    } else {
        sparse::residual(pool, K, x, f, r_);
        ++rep.spmvCount;
        bytes += spmvBytes + 16.0 * dn;
        double rel = sparse::norm2(pool, r_) / normF;
        bytes += 8.0 * dn;
        rep.residualHistory.push_back(rel);

        bool done = rel <= control.relTol;
        double rz = 0.0;
        if (!done) {
            m_->apply(r_, z_);
            rz = sparse::dot(pool, r_, z_);
            std::copy(z_.begin(), z_.end(), p_.begin());
            bytes += precBytes + 16.0 * dn + 16.0 * dn;
            if (!(rz > 0.0)) {
                rep.status = SolveStatus::Breakdown;
                rep.detail = "rᵀM⁻¹r = " + number(rz) +
                             " at the start: the preconditioner is not positive definite.";
                done = true;
            }
        }
        int windowStart = 0;
        double windowBest = rel;
        int k = 0;
        while (!done) {
            if (k >= control.maxIterations) {
                rep.status = SolveStatus::MaxIterations;
                rep.detail = "Reached the cap of " + std::to_string(control.maxIterations) +
                             " iterations at relative residual " + number(rel) + ".";
                break;
            }
            ++k;
            sparse::spmv(pool, K, p_, q_);
            ++rep.spmvCount;
            const double pq = sparse::dot(pool, p_, q_);
            bytes += spmvBytes + 16.0 * dn;
            if (!(pq > 0.0)) {
                rep.status = SolveStatus::Breakdown;
                rep.detail = "pᵀKp = " + number(pq) + " at iteration " + std::to_string(k) +
                             ": K is not symmetric positive definite.";
                --k; // this iteration did not complete
                break;
            }
            const double a = rz / pq;
            alpha.push_back(a);
            sparse::axpy(pool, a, p_, x);
            sparse::axpy(pool, -a, q_, r_);
            rel = sparse::norm2(pool, r_) / normF;
            bytes += 24.0 * dn + 24.0 * dn + 8.0 * dn;
            rep.residualHistory.push_back(rel);

            if (control.observer != nullptr) {
                const auto o0 = Clock::now();
                control.observer->onIteration(k, rel, x);
                observerSeconds += secondsSince(o0);
            }
            rep.iterTimestamps.push_back(solverSeconds());

            if (!std::isfinite(rel)) {
                rep.status = SolveStatus::Breakdown;
                rep.detail =
                    "The residual became NaN or infinite at iteration " + std::to_string(k) + ".";
                break;
            }
            if (rel <= control.relTol) {
                break; // converged
            }
            if (rel < windowBest / 10.0) {
                windowBest = rel;
                windowStart = k;
            } else if (control.stagnationWindow > 0 &&
                       k - windowStart >= control.stagnationWindow) {
                rep.status = SolveStatus::Stagnated;
                rep.detail = "The residual did not drop 10× within " +
                             std::to_string(control.stagnationWindow) +
                             " iterations (relative residual " + number(rel) + ").";
                break;
            }
            if (rep.iterTimestamps.back() > control.wallTimeCap) {
                rep.status = SolveStatus::TimeCap;
                rep.detail = "Reached the wall-time cap of " + number(control.wallTimeCap) +
                             " s after " + std::to_string(k) + " iterations.";
                break;
            }
            if (control.cancel != nullptr && control.cancel->cancelled()) {
                rep.status = SolveStatus::Cancelled;
                rep.detail = "Cancelled after " + std::to_string(k) + " iterations.";
                break;
            }

            m_->apply(r_, z_);
            const double rzNew = sparse::dot(pool, r_, z_);
            bytes += precBytes + 16.0 * dn;
            if (!(rzNew > 0.0)) {
                rep.status = SolveStatus::Breakdown;
                rep.detail = "rᵀM⁻¹r = " + number(rzNew) + " at iteration " + std::to_string(k) +
                             ": the preconditioner is not positive definite.";
                break;
            }
            const double b = rzNew / rz;
            beta.push_back(b);
            sparse::xpay(pool, z_, b, p_);
            bytes += 24.0 * dn;
            rz = rzNew;
        }
        rep.iterations = k;
    }
    rep.seconds = solverSeconds();
    rep.observerSeconds = observerSeconds;
    rep.bytesMoved = bytes;
    rep.workSpmvEq = spmvBytes > 0.0 ? bytes / spmvBytes : 0.0;
    rep.kappaEstimate = kappaFromCg(alpha, beta);

    // Recomputed after the timed part: the true residual and its sum (heat imbalance).
    sparse::residual(pool, K, x, f, r_);
    rep.finalTrueRelResidual = normF > 0.0 ? sparse::norm2(pool, r_) / normF : 0.0;
    if (control.residualSumMask != nullptr) {
        const auto& mask = *control.residualSumMask;
        double s = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            if (mask[i] != 0) {
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
