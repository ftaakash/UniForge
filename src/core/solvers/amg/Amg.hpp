#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/solvers/LinearSolver.hpp"
#include "core/solvers/direct/Cholesky.hpp"
#include "core/solvers/krylov/Preconditioner.hpp"

namespace uniforge::solvers {

// Smoothed-aggregation algebraic multigrid (build spec §5.4) after Vaněk, Mandel and Brezina,
// Computing 56 (1996), doi 10.1007/BF02238511; written from the description, not copied. It
// coarsens from the matrix alone, so it works for any mesh of any design.
//
// Setup, level by level until at most amgCoarseSize rows remain:
//   1. strength    j is a strong neighbour of i if |a_ij| ≥ θ_l·sqrt(|a_ii·a_jj|), with
//                  θ_l = θ·decay^l halving per level by default (without it the convergence
//                  factor grew with the mesh: 0.31 → 0.81 from 10³ to 32³ in the V2-7 tests)
//   2. aggregates  greedy: (a) a node whose strong neighbours are all free starts an aggregate
//                  with them; (b) remaining nodes join the aggregate of their strongest
//                  aggregated neighbour; (c) leftovers form aggregates with their free strong
//                  neighbours. Nodes without strong neighbours (e.g. Dirichlet identity rows)
//                  stay out: the smoother solves them exactly.
//   3. tentative P0 = 1 on (node, its aggregate): piecewise-constant, so a constant temperature
//                  (the near-null space of conduction with weak convection) is represented
//                  exactly on every coarse level.
//   4. smoothing   P = (I − ω·D_F⁻¹·A_F)·P0, A_F = A with weak couplings lumped onto the
//                  diagonal, ω = 4/(3ρ) with ρ ≥ ρ(D_F⁻¹·A_F) from Gershgorin's bound.
//   5. Galerkin    A_c = Pᵀ·(A·P) with two sparse products (sparse/SparseOps).
// The coarsest matrix is factorised by the direct solver.
//
// V-cycle (amgSweeps = ν): ν forward Gauss–Seidel sweeps, restrict the residual with Pᵀ,
// recurse, prolong with P, ν backward Gauss–Seidel sweeps. Forward before and backward after,
// with an exact coarse solve, make the cycle a symmetric positive definite operator, so it is
// a valid CG preconditioner (amendment A15).

// Building blocks, exposed for the tests. With a pool, strength and the smoothed prolongator run
// their row loops in parallel (V3c); the result is bit-identical to the serial call.
[[nodiscard]] std::vector<char> strongConnections(const CsrMatrix& A, double theta,
                                                  sparse::KernelPool* pool = nullptr);
// aggregateOf[i] = aggregate of node i, or −1 (no strong neighbours); `count` = aggregates.
[[nodiscard]] std::vector<Index> aggregate(const CsrMatrix& A, const std::vector<char>& strong,
                                           Index& count);
[[nodiscard]] CsrMatrix tentativeProlongator(const std::vector<Index>& aggregateOf, Index count);
[[nodiscard]] CsrMatrix smoothedProlongator(const CsrMatrix& A, const std::vector<char>& strong,
                                            const CsrMatrix& P0,
                                            sparse::KernelPool* pool = nullptr);

// Geometric coarsening (V3d). `grid` holds the original tensor-grid index (i, j, k) of every
// node of the current level; coarse nodes are those with all indices multiples of `stride`
// (2, 4, 8, … on levels 0, 1, 2, …). A fine node interpolates linearly along the grid lines
// (`lines[a][i]` = coordinate of line i on axis a) from the coarse nodes at the corners of its
// stride box. Corners that are not nodes (holes in the cell mask) are dropped and the weights
// renormalised, so constants are kept; a node with no corner left becomes a coarse node itself.
// Rows of A holding only a diagonal (eliminated Dirichlet nodes) get an empty row. Returns P;
// `coarseGrid` receives the grid indices of the coarse nodes, in coarse order.
[[nodiscard]] CsrMatrix geometricProlongator(const CsrMatrix& A,
                                             const std::vector<std::array<std::int32_t, 3>>& grid,
                                             std::int32_t stride,
                                             const std::array<std::vector<double>, 3>& lines,
                                             std::vector<std::array<std::int32_t, 3>>& coarseGrid,
                                             sparse::KernelPool* pool = nullptr);

class AmgHierarchy {
public:
    // Errors: misuse only (not square, NaN). Breakdown status if the coarsest factorisation fails
    // or geometric coarsening has no usable hint (detail says why).
    Result<SetupReport> build(const CsrMatrix& K, sparse::KernelPool& pool,
                              const SolverOptions& options, const CoordinateHint& hint = {});

    // One V-cycle for A_0·x = b, improving x in place (x is the initial guess).
    void vcycle(const Vector& b, Vector& x);

    // One V-cycle for k right-hand sides at once (V3e), zero initial guess. B and X hold k
    // values per row, interleaved (row i, case c at i·k + c), so every matrix entry is read once
    // for all k cases. Block kernels exist for the single-precision Chebyshev hierarchy; other
    // hierarchies run k ordinary V-cycles. 1 ≤ k ≤ kMaxBlock.
    static constexpr int kMaxBlock = 32;
    void vcycleBlock(const Vector& B, Vector& X, int k);
    [[nodiscard]] std::size_t blockBytes() const noexcept;

    [[nodiscard]] std::size_t levelCount() const noexcept { return ops_.size(); }
    [[nodiscard]] const CsrMatrix& op(std::size_t level) const noexcept { return *ops_[level]; }
    [[nodiscard]] const std::vector<Index>& aggregates(std::size_t level) const noexcept {
        return levels_[level].aggregateOf;
    }
    [[nodiscard]] MemoryFootprint footprint() const;
    [[nodiscard]] double bytesPerCycle() const noexcept { return bytesPerCycle_; }
    // True when the V-cycle runs in single precision (amgSinglePrecision). The double values of
    // op(l) for 0 < l < coarsest are then released; patterns and the coarsest op stay intact.
    [[nodiscard]] bool singlePrecision() const noexcept { return single_; }
    [[nodiscard]] AmgSmoother smoother() const noexcept { return smoother_; }
    // λ̂ used by the Chebyshev smoother on a non-coarsest level (0 with Gauss–Seidel).
    [[nodiscard]] double chebyshevLambdaMax(std::size_t level) const noexcept {
        return levels_[level].lambdaMax;
    }

private:
    struct Level {
        CsrMatrix P;
        CsrMatrix R; // Pᵀ
        std::vector<Index> aggregateOf;
        Vector r, tmp; // work: residual, prolonged correction (size of this level)
        Vector bc, xc; // right-hand side and solution of the next coarser level
        // Chebyshev smoother (V3b): 1/a_ii, search direction, and λ̂ ≥ λ_max(D⁻¹A).
        Vector invDiag, d;
        double lambdaMax = 0.0;
    };
    void cycle(std::size_t level, const Vector& b, Vector& x);

    // Single-precision copy of the hierarchy (V3a, amgSinglePrecision). The patterns (rowPtr,
    // col) are shared with the double matrices; only the values are stored as float, and the
    // double values of the transfers and of all coarse operators but the coarsest are released.
    struct FloatCsr {
        Index rows = 0;
        Index cols = 0;
        const std::vector<sparse::Offset>* rowPtr = nullptr;
        const std::vector<Index>* col = nullptr;
        std::vector<float> val;
    };
    struct FloatLevel {
        FloatCsr A, P, R;        // P, R empty on the coarsest level
        std::vector<float> b, x; // right-hand side and iterate of this level
        std::vector<float> r, tmp;
        std::vector<float> invDiag, d; // Chebyshev smoother
        // Block work vectors (V3e, k values per row): right-hand side, iterate, residual,
        // prolonged correction, Chebyshev direction.
        std::vector<float> bB, xB, rB, tmpB, dB;
    };
    void cycleSingleBlock(std::size_t level, int k);
    void buildSinglePrecision();
    void cycleSingle(std::size_t level);
    void buildChebyshev();

    sparse::KernelPool* pool_ = nullptr;
    int sweeps_ = 1;
    AmgSmoother smoother_ = AmgSmoother::GaussSeidel;
    int chebDegree_ = 2;
    double chebRatio_ = 30.0;
    int chebPowerIterations_ = 20;
    bool single_ = false;
    std::vector<FloatLevel> fl_;
    Vector coarseRhs_;
    std::vector<const CsrMatrix*> ops_; // ops_[0] = K (not owned), then owned coarse ops
    std::vector<std::unique_ptr<CsrMatrix>> owned_;
    std::vector<Level> levels_; // levels_[l] maps level l+1 → l
    std::unique_ptr<DirectCholesky> coarse_;
    Vector coarseSolution_;
    double bytesPerCycle_ = 0.0;
};

// One V-cycle with zero initial guess as the preconditioner of CG: AMG-PCG.
class AmgPreconditioner final : public Preconditioner {
public:
    explicit AmgPreconditioner(SolverOptions options = {})
        : options_(options) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return options_.coarsening == Coarsening::Geometric ? "GMG V-cycle" : "AMG V-cycle";
    }
    void setHint(const CoordinateHint& hint) override { hint_ = hint; }
    SetupReport setup(const CsrMatrix& K, sparse::KernelPool& pool) override;
    void apply(const Vector& r, Vector& z) override;
    [[nodiscard]] MemoryFootprint footprint() const override { return h_.footprint(); }
    [[nodiscard]] double bytesPerApply() const noexcept override { return h_.bytesPerCycle(); }
    [[nodiscard]] const AmgHierarchy& hierarchy() const noexcept { return h_; }

private:
    SolverOptions options_;
    CoordinateHint hint_;
    AmgHierarchy h_;
};

// AMG as a stand-alone solver: x ← x + V-cycle correction until the relative residual reaches
// the tolerance. Failure statuses as for PCG, plus Diverging when the residual grows for three
// cycles in a row and is more than 10× above its best value.
// AMG-PCG-CH-SP for several load cases at once (V3e): k preconditioned CG recurrences run in
// lockstep with block matrix products and block V-cycles, so K and the hierarchy are read once
// per iteration for all k cases. Each case keeps its own α, β and stopping test; a converged
// case stops changing. solve() is the k = 1 case. Memory: 4 double block vectors (R, Z, P, Q)
// plus the hierarchy's float block vectors; setup budget checks cover one case, solveMany
// refuses (OverBudget) when the block vectors would exceed the budget.
class AmgBatchPcg final : public LinearOperatorSolver {
public:
    AmgBatchPcg() = default;

    [[nodiscard]] SolverKind kind() const noexcept override {
        return SolverKind::AmgPcgChebSpBatch;
    }
    Result<SetupReport> setup(const CsrMatrix& K, const CoordinateHint& hint,
                              sparse::KernelPool& pool, const SolverOptions& options) override;
    Result<SolveReport> solve(const Vector& f, Vector& x, const SolveControl& control) override;
    Result<SolveReport> solveMany(const std::vector<Vector>& F, std::vector<Vector>& X,
                                  const SolveControl& control) override;
    [[nodiscard]] MemoryFootprint footprint() const override;

private:
    AmgHierarchy h_;
    const CsrMatrix* k_ = nullptr;
    sparse::KernelPool* pool_ = nullptr;
    std::size_t budget_ = 0;
    bool ready_ = false;
    Vector R_, Z_, P_, Q_, Xb_, Fb_; // interleaved block vectors (n·k)
};

class AmgSolver final : public LinearOperatorSolver {
public:
    AmgSolver() = default;

    [[nodiscard]] SolverKind kind() const noexcept override { return SolverKind::Amg; }
    Result<SetupReport> setup(const CsrMatrix& K, const CoordinateHint& hint,
                              sparse::KernelPool& pool, const SolverOptions& options) override;
    Result<SolveReport> solve(const Vector& f, Vector& x, const SolveControl& control) override;
    [[nodiscard]] MemoryFootprint footprint() const override;
    [[nodiscard]] const AmgHierarchy& hierarchy() const noexcept { return h_; }

private:
    AmgHierarchy h_;
    const CsrMatrix* k_ = nullptr;
    sparse::KernelPool* pool_ = nullptr;
    bool ready_ = false;
    Vector r_;
};

} // namespace uniforge::solvers
