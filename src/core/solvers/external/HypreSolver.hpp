#pragma once

#include <memory>

#include "core/solvers/LinearSolver.hpp"

namespace uniforge::solvers {

// Reference solver (V3f): hypre's PCG preconditioned by one BoomerAMG V-cycle per iteration,
// on a copy of K in hypre's ParCSR format (one process, no MPI; OpenMP threads = pool threads).
// hypre is an optional external library (UNIFORGE_WITH_HYPRE, docs/v3_experiments.md); without
// it every call returns InvalidInput, so the core builds and tests without hypre.
//
// BoomerAMG settings (fixed, from hypre's documentation for 3D diffusion): HMIS coarsening,
// extended+i interpolation with at most 4 entries per row, hybrid symmetric Gauss–Seidel (one
// sweep down and up, so the preconditioner is symmetric), strength threshold `theta`
// (0.25 = hypre's default; 0.5 = hypre's suggestion for 3D). Stopping test as in our PCG:
// ‖r_k‖₂ ≤ relTol·‖f‖₂ on the recursive residual (hypre two-norm mode).
class HyprePcg final : public LinearOperatorSolver {
public:
    HyprePcg(SolverKind kind, double theta);
    ~HyprePcg() override;

    [[nodiscard]] static bool available() noexcept;

    [[nodiscard]] SolverKind kind() const noexcept override { return kind_; }
    Result<SetupReport> setup(const CsrMatrix& K, const CoordinateHint& hint,
                              sparse::KernelPool& pool, const SolverOptions& options) override;
    Result<SolveReport> solve(const Vector& f, Vector& x, const SolveControl& control) override;
    [[nodiscard]] MemoryFootprint footprint() const override;

private:
    struct Impl;
    SolverKind kind_;
    double theta_;
    std::unique_ptr<Impl> impl_;
};

} // namespace uniforge::solvers
