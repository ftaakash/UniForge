#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/common/CancelToken.hpp"
#include "core/common/Progress.hpp"
#include "core/sparse/CsrMatrix.hpp"
#include "core/sparse/VectorOps.hpp"

namespace uniforge::solvers {

using sparse::CsrMatrix;
using sparse::Index;
using sparse::Vector;

// The six solver variants compared in V2 (build spec §5, labels used in every table and plot),
// plus the V3 candidates: AmgPcgSp (single-precision hierarchy, V3a), AmgPcgCheb and
// AmgPcgChebSp (Chebyshev–Jacobi smoother instead of Gauss–Seidel, double / single, V3b),
// GmgPcg and GmgPcgChebSp (geometric coarsening on the mesh grid instead of aggregation, V3d),
// AmgPcgChebSpBatch (AMG-PCG-CH-SP solving several load cases in lockstep, V3e).
enum class SolverKind {
    DirectND,
    DirectRCM,
    PcgJacobi,
    PcgIc0,
    Amg,
    AmgPcg,
    AmgPcgSp,
    AmgPcgCheb,
    AmgPcgChebSp,
    GmgPcg,
    GmgPcgChebSp,
    AmgPcgChebSpBatch
};

enum class AmgSmoother { GaussSeidel, Chebyshev };

// How the multigrid hierarchy is coarsened: smoothed aggregation from the matrix alone (V2), or
// geometrically from the tensor-grid indices of the nodes (V3d, needs CoordinateHint::grid and
// ::xyz): every second grid line per axis, linear interpolation along the lines.
enum class Coarsening { Aggregation, Geometric };

[[nodiscard]] constexpr std::string_view label(SolverKind kind) noexcept {
    switch (kind) {
    case SolverKind::DirectND:
        return "DIRECT-ND";
    case SolverKind::DirectRCM:
        return "DIRECT-RCM";
    case SolverKind::PcgJacobi:
        return "PCG-J";
    case SolverKind::PcgIc0:
        return "PCG-IC0";
    case SolverKind::Amg:
        return "AMG";
    case SolverKind::AmgPcg:
        return "AMG-PCG";
    case SolverKind::AmgPcgSp:
        return "AMG-PCG-SP";
    case SolverKind::AmgPcgCheb:
        return "AMG-PCG-CH";
    case SolverKind::AmgPcgChebSp:
        return "AMG-PCG-CH-SP";
    case SolverKind::GmgPcg:
        return "GMG-PCG";
    case SolverKind::GmgPcgChebSp:
        return "GMG-PCG-CH-SP";
    case SolverKind::AmgPcgChebSpBatch:
        return "AMG-PCG-CH-SP-B";
    }
    return "?";
}

// How a setup or solve ended (build spec §10). A failure of the method is a status, not an
// Error: Error is reserved for misuse (wrong sizes, NaN input) and I/O.
enum class SolveStatus {
    Converged,
    MaxIterations, // iteration cap reached
    Stagnated,     // the best residual did not drop 10× within the stagnation window
    Breakdown,     // the method cannot continue (e.g. pᵀKp ≤ 0: matrix not SPD)
    OverBudget,    // the memory needed exceeds the budget (detected before allocating)
    TimeCap,       // wall-time cap reached
    Cancelled,
    Diverging, // residual grows from cycle to cycle (AMG)
};

[[nodiscard]] constexpr std::string_view label(SolveStatus s) noexcept {
    switch (s) {
    case SolveStatus::Converged:
        return "Converged";
    case SolveStatus::MaxIterations:
        return "MaxIterations";
    case SolveStatus::Stagnated:
        return "Stagnated";
    case SolveStatus::Breakdown:
        return "Breakdown";
    case SolveStatus::OverBudget:
        return "OverBudget";
    case SolveStatus::TimeCap:
        return "TimeCap";
    case SolveStatus::Cancelled:
        return "Cancelled";
    case SolveStatus::Diverging:
        return "Diverging";
    }
    return "?";
}

// Bytes held by a solver, by named component (build spec §6.2), from capacity() × element size.
struct MemoryFootprint {
    std::vector<std::pair<std::string, std::size_t>> parts;

    void add(std::string name, std::size_t bytes) { parts.emplace_back(std::move(name), bytes); }
    [[nodiscard]] std::size_t total() const noexcept {
        std::size_t t = 0;
        for (const auto& p : parts) {
            t += p.second;
        }
        return t;
    }
};

// Node coordinates and grid indices as plain arrays, so solvers never include fem3d (build
// spec §4.2). Used by the direct solver's grid-plane ordering fallback (V2-5).
struct CoordinateHint {
    const std::vector<std::array<double, 3>>* xyz = nullptr;
    const std::vector<std::array<std::int32_t, 3>>* grid = nullptr;
};

// Per-kind settings (build spec §6.2). Fields a kind does not use are ignored.
struct SolverOptions {
    // Setup refuses (status OverBudget) when the solver's own memory would exceed this.
    std::size_t memoryBudgetBytes = std::numeric_limits<std::size_t>::max();
    // Direct solver: leaf size below which nested dissection stops splitting a subgraph.
    int dissectionLeafSize = 64;
    // Smoothed-aggregation AMG (build spec §5.4).
    // j is a strong neighbour of i if |a_ij| ≥ θ·sqrt(a_ii·a_jj). 0.02, not the 0.08 tuned in
    // V2-7 on a 7-point finite-difference Laplacian: for trilinear hexahedra of aspect ratio 1
    // the largest coupling is 1/16 of the diagonal, so 0.08 made every coupling weak and AMG
    // stagnated (V2-9 candidate check, docs/v2_experiments.md).
    double amgStrengthThreshold = 0.02;
    double amgStrengthDecay = 0.5; // θ on level l = amgStrengthThreshold · decay^l
    int amgCoarseSize = 500;       // stop coarsening at this many rows (then direct solve)
    int amgMaxLevels = 20;
    int amgSweeps = 1; // Gauss–Seidel sweeps before and after the coarse correction
    // V3a: store and apply the hierarchy (operators, transfers, work vectors) in single
    // precision; the coarsest factorisation and the outer CG stay in double. Only for AMG as a
    // preconditioner: stand-alone AMG ignores it (its iterate must stay in double precision).
    bool amgSinglePrecision = false;
    // V3b: smoother of the V-cycle. Gauss–Seidel (V2) is serial; Chebyshev–Jacobi uses only
    // matrix–vector products and vector updates, so it runs on the kernel pool's threads
    // (Adams, Brezina, Hu, Tuminaro, J. Comput. Phys. 188 (2003)). The polynomial of degree
    // amgChebyshevDegree targets the eigenvalues of D⁻¹A in [λ̂/ratio, λ̂], λ̂ = 1.1 × a power
    // iteration estimate of λ_max(D⁻¹A). amgSweeps applies to Gauss–Seidel only.
    AmgSmoother amgSmoother = AmgSmoother::GaussSeidel;
    int amgChebyshevDegree = 2;
    double amgChebyshevRatio = 30.0;
    int amgChebyshevPowerIterations = 20; // 10 under-estimates λ_max on HS-7 (V3c: CG breakdown)
    // V3d: aggregation (AMG) or geometric coarsening (GMG). Geometric needs the hint.
    Coarsening coarsening = Coarsening::Aggregation;
};

class IterationObserver;

// Controls of one solve (build spec §6.2, §7).
struct SolveControl {
    double relTol = 1e-10; // stop when ‖f − K·x‖₂ ≤ relTol · ‖f‖₂ (recursive residual)
    int maxIterations = 10000;
    int stagnationWindow = 2000; // iterations without a 10× drop of the best residual; 0 = off
    double wallTimeCap = std::numeric_limits<double>::infinity(); // seconds of solver time
    IterationObserver* observer = nullptr;
    const CancelToken* cancel = nullptr;
    // If set, residualSum only adds the rows where the mask is non-zero (the free rows; rows of
    // eliminated Dirichlet values are excluded). Size must equal the number of rows.
    const std::vector<std::uint8_t>* residualSumMask = nullptr;
};

struct SetupReport {
    SolveStatus status = SolveStatus::Converged; // Converged = ready; OverBudget, Breakdown
    std::string detail;
    double seconds = 0.0;
    MemoryFootprint footprint;
    // Time of each setup step, e.g. {"ordering", 0.12}, {"symbolic", 0.05}, {"numeric", 3.4}.
    std::vector<std::pair<std::string, double>> phases;
    // Factorisations (direct solver): entries of the factor L (diagonal included), the number the
    // symbolic analysis predicted before allocating, and the floating-point operations of the
    // numeric factorisation (Σ over columns of count²). 0 when not applicable.
    std::int64_t factorNnz = 0;
    std::int64_t predictedFactorNnz = 0;
    double factorFlops = 0.0;
    // Incomplete factorisations (IC(0)): the diagonal shift α finally used (K + α·diag(K) was
    // factorised) and how many shifts failed before it (build spec §5.3, amendment A7).
    double preconditionerShift = 0.0;
    int preconditionerRetries = 0;
    // Multigrid hierarchy (AMG): rows and stored entries per level (level 0 = K), and
    // operator complexity Σ nnz(A_l)/nnz(A_0) and grid complexity Σ rows(A_l)/rows(A_0).
    std::vector<std::pair<Index, std::int64_t>> levels;
    double operatorComplexity = 0.0;
    double gridComplexity = 0.0;
};

struct SolveReport {
    SolveStatus status = SolveStatus::Converged;
    std::string detail; // failure detail, e.g. "pᵀKp = −3.2e-5 at iteration 17"
    int iterations = 0;
    // Solver time only: wall time minus the time spent inside the observer (build spec §7).
    double seconds = 0.0;
    double observerSeconds = 0.0;
    // Work (build spec §7, amendment A9): matrix–vector products, bytes moved by all kernels
    // (estimated from array sizes), and the same expressed in SpMV-equivalents.
    std::int64_t spmvCount = 0;
    double bytesMoved = 0.0;
    double workSpmvEq = 0.0;
    std::vector<double> residualHistory; // ‖r_k‖₂ / ‖f‖₂, entry 0 = initial
    std::vector<double> iterTimestamps;  // solver seconds at the end of each iteration
    double kappaEstimate = 0.0;          // κ(M⁻¹K) from the CG–Lanczos tridiagonal; 0 = none
    double finalTrueRelResidual = 0.0;   // ‖f − K·x‖₂ / ‖f‖₂, recomputed after the solve
    double residualSum = 0.0;            // Σ (f − K·x)_i over the masked rows: heat imbalance, W
};

} // namespace uniforge::solvers
