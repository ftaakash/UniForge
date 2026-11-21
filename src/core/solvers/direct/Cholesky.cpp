#include "core/solvers/direct/Cholesky.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <string>
#include <utility>

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
    s.precision(4);
    s << v;
    return s.str();
}

// Lower triangle (diagonal included) of P·K·Pᵀ in CSR: row k holds the entries (k, j), j ≤ k.
struct LowerPermuted {
    std::vector<Offset> rowPtr;
    std::vector<Index> col;
    std::vector<double> val;
};

LowerPermuted permuteLower(const CsrMatrix& K, const Permutation& p) {
    const Index n = K.rows;
    LowerPermuted c;
    c.rowPtr.assign(toSize(n) + 1, 0);
    for (Index k = 0; k < n; ++k) {
        const Index old = p.perm[toSize(k)];
        Offset count = 0;
        for (Offset q = K.rowPtr[toSize(old)]; q < K.rowPtr[toSize(old) + 1]; ++q) {
            if (p.inverse[toSize(K.col[toSize(q)])] <= k) {
                ++count;
            }
        }
        c.rowPtr[toSize(k) + 1] = c.rowPtr[toSize(k)] + count;
    }
    c.col.resize(toSize(c.rowPtr.back()));
    c.val.resize(toSize(c.rowPtr.back()));
    for (Index k = 0; k < n; ++k) {
        const Index old = p.perm[toSize(k)];
        Offset at = c.rowPtr[toSize(k)];
        for (Offset q = K.rowPtr[toSize(old)]; q < K.rowPtr[toSize(old) + 1]; ++q) {
            const Index j = p.inverse[toSize(K.col[toSize(q)])];
            if (j <= k) {
                c.col[toSize(at)] = j;
                c.val[toSize(at)] = K.val[toSize(q)];
                ++at;
            }
        }
    }
    return c;
}

// Pattern of row k of L (excluding the diagonal) by walking the elimination tree up from every
// j < k with C(k, j) ≠ 0 until a node already reached for this row. The result s[top … n) is in
// topological order: a node comes before its ancestors.
Index rowPattern(const LowerPermuted& c, const std::vector<Index>& parent, Index k,
                 std::vector<Index>& mark, std::vector<Index>& path, std::vector<Index>& s) {
    const Index n = static_cast<Index>(parent.size());
    Index top = n;
    mark[toSize(k)] = k;
    for (Offset q = c.rowPtr[toSize(k)]; q < c.rowPtr[toSize(k) + 1]; ++q) {
        Index i = c.col[toSize(q)];
        if (i >= k) {
            continue;
        }
        std::size_t len = 0;
        while (mark[toSize(i)] != k) {
            path[len++] = i;
            mark[toSize(i)] = k;
            i = parent[toSize(i)];
        }
        while (len > 0) {
            s[toSize(--top)] = path[--len];
        }
    }
    return top;
}

} // namespace

DirectCholesky::DirectCholesky(SolverKind kind, Ordering ordering)
    : kind_(kind)
    , ordering_(ordering) {}

Result<SetupReport> DirectCholesky::setup(const CsrMatrix& K, const CoordinateHint& /*hint*/,
                                          sparse::KernelPool& pool, const SolverOptions& options) {
    ready_ = false;
    l_ = Factor{};
    work_ = Vector{};
    if (K.rows != K.cols) {
        return makeError(ErrorCode::InvalidInput,
                         "Cholesky needs a square matrix, got " + std::to_string(K.rows) + " × " +
                             std::to_string(K.cols) + ".",
                         "This is a programming error in the caller.");
    }
    for (const double v : K.val) {
        if (!std::isfinite(v)) {
            return makeError(ErrorCode::NonFinite, "The matrix contains NaN or infinite entries.",
                             "Check the assembly inputs.");
        }
    }
    pool_ = &pool;
    const Index n = K.rows;
    SetupReport report;
    const auto t0 = Clock::now();

    // 1. Ordering
    auto t = Clock::now();
    Permutation p = makeOrdering(ordering_, K, options.dissectionLeafSize);
    report.phases.emplace_back("ordering", secondsSince(t));

    // 2. Symbolic: elimination tree and column counts of L.
    t = Clock::now();
    const LowerPermuted c = permuteLower(K, p);
    std::vector<Index> parent(toSize(n), -1);
    {
        std::vector<Index> ancestor(toSize(n), -1);
        for (Index k = 0; k < n; ++k) {
            for (Offset q = c.rowPtr[toSize(k)]; q < c.rowPtr[toSize(k) + 1]; ++q) {
                Index i = c.col[toSize(q)];
                while (i != -1 && i < k) {
                    const Index next = ancestor[toSize(i)];
                    ancestor[toSize(i)] = k;
                    if (next == -1) {
                        parent[toSize(i)] = k;
                    }
                    i = next;
                }
            }
        }
    }
    std::vector<std::int64_t> counts(toSize(n), 1); // the diagonal
    std::vector<Index> mark(toSize(n), -1);
    std::int64_t total = n;
    const std::size_t fixedBytes = c.col.capacity() * (sizeof(Index) + sizeof(double)) +
                                   (toSize(n) + 1) * sizeof(std::int64_t) +
                                   toSize(n) * (sizeof(double) + 3 * sizeof(Index));
    const auto bytesFor = [&](std::int64_t entries) {
        return fixedBytes + static_cast<std::size_t>(entries) * (sizeof(double) + sizeof(Index));
    };
    bool overBudget = false;
    for (Index k = 0; k < n && !overBudget; ++k) {
        mark[toSize(k)] = k;
        for (Offset q = c.rowPtr[toSize(k)]; q < c.rowPtr[toSize(k) + 1]; ++q) {
            Index i = c.col[toSize(q)];
            if (i >= k) {
                continue;
            }
            while (mark[toSize(i)] != k) {
                mark[toSize(i)] = k;
                ++counts[toSize(i)];
                ++total;
                i = parent[toSize(i)];
            }
        }
        if (bytesFor(total) > options.memoryBudgetBytes) {
            overBudget = true; // stop counting: the factor is at least this large
        }
    }
    report.predictedFactorNnz = total;
    report.phases.emplace_back("symbolic", secondsSince(t));
    if (overBudget) {
        report.status = SolveStatus::OverBudget;
        report.detail = "The Cholesky factor needs at least " + std::to_string(total) +
                        " entries (" + std::to_string(bytesFor(total)) +
                        " bytes), more than the budget of " +
                        std::to_string(options.memoryBudgetBytes) + " bytes.";
        report.seconds = secondsSince(t0);
        return report;
    }
    for (const std::int64_t cnt : counts) {
        report.factorFlops += static_cast<double>(cnt) * static_cast<double>(cnt);
    }

    // 3. Numeric: up-looking Cholesky, one row of L at a time.
    t = Clock::now();
    Factor L;
    L.n = n;
    L.colPtr.assign(toSize(n) + 1, 0);
    for (Index j = 0; j < n; ++j) {
        L.colPtr[toSize(j) + 1] = L.colPtr[toSize(j)] + counts[toSize(j)];
    }
    L.rowIdx.resize(toSize(total));
    L.val.resize(toSize(total));
    std::vector<std::int64_t> next(L.colPtr.begin(), L.colPtr.end() - 1);
    std::vector<double> x(toSize(n), 0.0);
    std::vector<Index> path(toSize(n));
    std::vector<Index> s(toSize(n));
    std::fill(mark.begin(), mark.end(), -1);
    for (Index k = 0; k < n; ++k) {
        const Index top = rowPattern(c, parent, k, mark, path, s);
        for (Offset q = c.rowPtr[toSize(k)]; q < c.rowPtr[toSize(k) + 1]; ++q) {
            x[toSize(c.col[toSize(q)])] = c.val[toSize(q)];
        }
        double d = x[toSize(k)];
        x[toSize(k)] = 0.0;
        for (Index t2 = top; t2 < n; ++t2) {
            const Index j = s[toSize(t2)];
            const double lkj = x[toSize(j)] / L.val[toSize(L.colPtr[toSize(j)])];
            x[toSize(j)] = 0.0;
            for (std::int64_t q = L.colPtr[toSize(j)] + 1; q < next[toSize(j)]; ++q) {
                x[toSize(L.rowIdx[toSize(q)])] -= L.val[toSize(q)] * lkj;
            }
            d -= lkj * lkj;
            L.rowIdx[toSize(next[toSize(j)])] = k;
            L.val[toSize(next[toSize(j)])] = lkj;
            ++next[toSize(j)];
        }
        if (!(d > 0.0)) {
            report.status = SolveStatus::Breakdown;
            report.detail = "Cholesky pivot " + number(d) + " ≤ 0 at row " + std::to_string(k) +
                            " (matrix row " + std::to_string(p.perm[toSize(k)]) +
                            "): K is not symmetric positive definite.";
            report.phases.emplace_back("numeric", secondsSince(t));
            report.seconds = secondsSince(t0);
            return report;
        }
        L.rowIdx[toSize(next[toSize(k)])] = k;
        L.val[toSize(next[toSize(k)])] = std::sqrt(d);
        ++next[toSize(k)];
    }
    report.phases.emplace_back("numeric", secondsSince(t));

    L.parent = std::move(parent);
    L.permutation = std::move(p);
    l_ = std::move(L);
    work_.assign(toSize(n), 0.0);
    k_ = &K;
    ready_ = true;
    report.factorNnz = total;
    report.footprint = footprint();
    report.seconds = secondsSince(t0);
    return report;
}

MemoryFootprint DirectCholesky::footprint() const {
    MemoryFootprint f;
    f.add("Cholesky factor L", l_.colPtr.capacity() * sizeof(std::int64_t) +
                                   l_.rowIdx.capacity() * sizeof(Index) +
                                   l_.val.capacity() * sizeof(double));
    f.add("elimination tree and permutation",
          (l_.parent.capacity() + l_.permutation.perm.capacity() +
           l_.permutation.inverse.capacity()) *
              sizeof(Index));
    f.add("solve work vector", work_.capacity() * sizeof(double));
    return f;
}

Result<SolveReport> DirectCholesky::solve(const Vector& f, Vector& x, const SolveControl& control) {
    if (!ready_) {
        return makeError(ErrorCode::InvalidInput, "The Cholesky factor is not available.",
                         "Call setup() and check its status first.");
    }
    const CsrMatrix& K = *k_;
    const std::size_t n = toSize(K.rows);
    if (f.size() != n || x.size() != n ||
        (control.residualSumMask != nullptr && control.residualSumMask->size() != n)) {
        return makeError(ErrorCode::InvalidInput,
                         "Right-hand side, solution vector or mask does not match the " +
                             std::to_string(n) + " rows of K.",
                         "This is a programming error in the caller.");
    }
    for (const double v : f) {
        if (!std::isfinite(v)) {
            return makeError(ErrorCode::NonFinite, "The right-hand side contains NaN or infinity.",
                             "Check the load and boundary-condition inputs.");
        }
    }
    SolveReport rep;
    const auto t0 = Clock::now();
    const auto& perm = l_.permutation.perm;
    Vector& y = work_;
    for (std::size_t k = 0; k < n; ++k) {
        y[k] = f[toSize(perm[k])];
    }
    // L·z = y (forward), column by column.
    for (std::size_t j = 0; j < n; ++j) {
        const std::int64_t first = l_.colPtr[j];
        y[j] /= l_.val[toSize(first)];
        for (std::int64_t q = first + 1; q < l_.colPtr[j + 1]; ++q) {
            y[toSize(l_.rowIdx[toSize(q)])] -= l_.val[toSize(q)] * y[j];
        }
    }
    // Lᵀ·w = z (backward), column j of L is row j of Lᵀ.
    for (std::size_t j = n; j-- > 0;) {
        const std::int64_t first = l_.colPtr[j];
        double v = y[j];
        for (std::int64_t q = first + 1; q < l_.colPtr[j + 1]; ++q) {
            v -= l_.val[toSize(q)] * y[toSize(l_.rowIdx[toSize(q)])];
        }
        y[j] = v / l_.val[toSize(first)];
    }
    for (std::size_t k = 0; k < n; ++k) {
        x[toSize(perm[k])] = y[k];
    }
    rep.seconds = secondsSince(t0);
    const double nnzL = static_cast<double>(l_.val.size());
    rep.bytesMoved = 2.0 * 12.0 * nnzL + 4.0 * 8.0 * static_cast<double>(n);
    const double spmvBytes = 12.0 * static_cast<double>(K.nnz()) + 32.0 * static_cast<double>(n);
    rep.workSpmvEq = spmvBytes > 0.0 ? rep.bytesMoved / spmvBytes : 0.0;

    // True residual and its masked sum, after the timed part (as for PCG).
    Vector r(n);
    sparse::residual(*pool_, K, x, f, r);
    const double normF = sparse::norm2(*pool_, f);
    rep.finalTrueRelResidual = normF > 0.0 ? sparse::norm2(*pool_, r) / normF : 0.0;
    rep.residualHistory = {rep.finalTrueRelResidual};
    if (control.residualSumMask != nullptr) {
        double sum = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            if ((*control.residualSumMask)[i] != 0) {
                sum += r[i];
            }
        }
        rep.residualSum = sum;
    } else {
        rep.residualSum = sparse::sum(*pool_, r);
    }
    return rep;
}

} // namespace uniforge::solvers
