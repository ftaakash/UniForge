#pragma once

#include <vector>

#include "core/common/CancelToken.hpp"
#include "core/common/Result.hpp"
#include "core/thermal/Assembler.hpp"
#include "core/thermal/RodProblem.hpp"

namespace uniforge::thermal {

struct FeaSolution {
    std::vector<double> x; // node coordinates, m (size N + 1)
    std::vector<double> T; // nodal temperatures, °C (size N + 1)
    // Heat leaving the rod through each end, W, from the unconstrained equations (reactions).
    // Their sum equals the heat generated, q·A·L, to rounding.
    double heatOutLeft_W = 0.0;
    double heatOutRight_W = 0.0;
};

// Buffers reused across solves (the Phase 5 batch runner keeps one per worker), so repeated
// solves of the same size allocate nothing. Holds no other state: solveRod is reentrant.
struct RodWorkspace {
    TridiagonalSystem system;
    std::vector<double> scratch;
};

// Solves the rod problem with N two-node linear elements: validate → assemble → Dirichlet
// elimination → Thomas → end heat flows (spec §5.2).
// Post-conditions: x.size() == T.size() == N + 1; T[0] == T0 and T[N] == TL exactly.
// Errors: the problem's validate() errors; Cancelled when `cancel` is set before the solve.
[[nodiscard]] Result<FeaSolution> solveRod(const RodProblem& problem,
                                           const CancelToken* cancel = nullptr);

// Same, writing into `out` and using `workspace` for the linear system.
Result<void> solveRod(const RodProblem& problem, RodWorkspace& workspace, FeaSolution& out,
                      const CancelToken* cancel = nullptr);

} // namespace uniforge::thermal
