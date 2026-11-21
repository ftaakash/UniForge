#pragma once

#include <memory>

#include "core/common/Result.hpp"
#include "core/solvers/SolverTypes.hpp"
#include "core/sparse/KernelPool.hpp"

namespace uniforge::solvers {

// Common interface of the six V2 solver variants (build spec §5.1).
//
//   setup(K, …)       once per matrix: factorisation, preconditioner or AMG hierarchy
//   solve(f, x, …)    once per load case, reusing the setup
//
// The solver keeps a reference to K and to the pool given to setup(); both must outlive it.
// A failure of the method (breakdown, caps, over budget) is a status in the report; an Error is
// returned only for misuse: no setup, wrong sizes, or NaN/Inf in the input.
class LinearOperatorSolver {
public:
    virtual ~LinearOperatorSolver() = default;
    LinearOperatorSolver() = default;
    LinearOperatorSolver(const LinearOperatorSolver&) = delete;
    LinearOperatorSolver& operator=(const LinearOperatorSolver&) = delete;

    [[nodiscard]] virtual SolverKind kind() const noexcept = 0;

    // Errors: InvalidInput (K not square), NonFinite (NaN/Inf in K).
    virtual Result<SetupReport> setup(const CsrMatrix& K, const CoordinateHint& hint,
                                      sparse::KernelPool& pool, const SolverOptions& options) = 0;

    // x holds the initial guess on entry (its size must be K.rows) and the solution on return.
    // Errors: InvalidInput (no successful setup, wrong sizes), NonFinite (NaN/Inf in f or x).
    virtual Result<SolveReport> solve(const Vector& f, Vector& x, const SolveControl& control) = 0;

    // Bytes held by the solver after setup (work vectors included).
    [[nodiscard]] virtual MemoryFootprint footprint() const = 0;
};

// Creates a solver of the given kind. Kinds not built yet return InvalidInput naming the phase
// that adds them (V2-5 direct, V2-6 IC(0), V2-7 AMG).
[[nodiscard]] Result<std::unique_ptr<LinearOperatorSolver>> makeSolver(SolverKind kind);

} // namespace uniforge::solvers
