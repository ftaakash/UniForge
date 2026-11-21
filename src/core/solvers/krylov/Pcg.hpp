#pragma once

#include <memory>

#include "core/solvers/LinearSolver.hpp"
#include "core/solvers/krylov/Preconditioner.hpp"

namespace uniforge::solvers {

// Preconditioned conjugate gradients for symmetric positive definite K (build spec §5.3).
//
//   r = f − K·x,  z = M⁻¹·r,  p = z
//   repeat:  q = K·p;  α = (rᵀz)/(pᵀq);  x += α·p;  r −= α·q
//            stop if ‖r‖₂ ≤ relTol·‖f‖₂ (or a cap, stagnation, cancel)
//            z = M⁻¹·r;  β = (rᵀz)_new/(rᵀz)_old;  p = z + β·p
//
// Every vector operation uses the shared sparse kernels, so the iterates, the iteration count and
// the residual history are bit-identical for any thread count (spec N2). α and β are kept for the
// condition estimate (Lanczos.hpp). Failures are statuses: pᵀKp ≤ 0 or rᵀz ≤ 0 → Breakdown.
class Pcg final : public LinearOperatorSolver {
public:
    // `kind` is the label reported (PCG-J for the Jacobi preconditioner).
    Pcg(SolverKind kind, std::unique_ptr<Preconditioner> preconditioner);

    [[nodiscard]] SolverKind kind() const noexcept override { return kind_; }
    Result<SetupReport> setup(const CsrMatrix& K, const CoordinateHint& hint,
                              sparse::KernelPool& pool, const SolverOptions& options) override;
    Result<SolveReport> solve(const Vector& f, Vector& x, const SolveControl& control) override;
    [[nodiscard]] MemoryFootprint footprint() const override;

private:
    SolverKind kind_;
    std::unique_ptr<Preconditioner> m_;
    const CsrMatrix* k_ = nullptr;
    sparse::KernelPool* pool_ = nullptr;
    bool ready_ = false;
    Vector r_, z_, p_, q_; // work vectors, allocated once in setup()
};

} // namespace uniforge::solvers
