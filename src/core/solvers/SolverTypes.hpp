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

// The six solver variants compared in V2 (build spec §5, labels used in every table and plot).
enum class SolverKind { DirectND, DirectRCM, PcgJacobi, PcgIc0, Amg, AmgPcg };

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
