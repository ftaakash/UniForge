#pragma once

#include <cstdint>
#include <vector>

#include "core/solvers/LinearSolver.hpp"
#include "core/solvers/direct/Ordering.hpp"

namespace uniforge::solvers {

// Sparse direct solver (build spec §5.2): P·K·Pᵀ = L·Lᵀ, then two triangular solves.
//
// Setup, in three timed steps:
//   1. ordering   fill-reducing permutation P (natural, RCM or nested dissection)
//   2. symbolic   elimination tree of P·K·Pᵀ and the exact number of entries in every column of
//                 L, from the row patterns ("row subtrees" of the tree). The memory needed is
//                 known here, before L is allocated: over the budget → status OverBudget.
//   3. numeric    up-looking Cholesky: row k of L solves L(0:k,0:k)·l = column k of P·K·Pᵀ by
//                 a sparse triangular solve over the row's pattern, then l_kk = √(d). A pivot
//                 d ≤ 0 means K is not positive definite → status Breakdown with its row.
// [VERIFY: algorithms as in Davis, Direct Methods for Sparse Linear Systems, SIAM 2006, ch. 4;
//  written here from the description, not copied.]
//
// L is stored by columns: colPtr (64-bit, the factor may exceed 2^31 entries), rowIdx, val,
// with the diagonal first in each column. The solve is serial and exact up to rounding; its
// cost does not depend on the conditioning of K, only on nnz(L).
class DirectCholesky final : public LinearOperatorSolver {
public:
    DirectCholesky(SolverKind kind, Ordering ordering);

    [[nodiscard]] SolverKind kind() const noexcept override { return kind_; }
    Result<SetupReport> setup(const CsrMatrix& K, const CoordinateHint& hint,
                              sparse::KernelPool& pool, const SolverOptions& options) override;
    Result<SolveReport> solve(const Vector& f, Vector& x, const SolveControl& control) override;
    [[nodiscard]] MemoryFootprint footprint() const override;

    // The factor, for tests and pictures (valid after a successful setup).
    struct Factor {
        Index n = 0;
        std::vector<std::int64_t> colPtr;
        std::vector<Index> rowIdx;
        std::vector<double> val;
        std::vector<Index> parent; // elimination tree, −1 = root
        Permutation permutation;
    };
    [[nodiscard]] const Factor& factor() const noexcept { return l_; }

private:
    SolverKind kind_;
    Ordering ordering_;
    const CsrMatrix* k_ = nullptr;
    sparse::KernelPool* pool_ = nullptr; // for the residual check after a solve
    bool ready_ = false;
    Factor l_;
    Vector work_; // permuted right-hand side / solution
};

} // namespace uniforge::solvers
