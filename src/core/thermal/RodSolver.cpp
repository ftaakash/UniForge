#include "core/thermal/RodSolver.hpp"

#include "core/thermal/DirichletBC.hpp"
#include "core/thermal/Mesh1D.hpp"
#include "core/thermal/ThomasSolver.hpp"

namespace uniforge::thermal {

namespace {

Error cancelledError() {
    return makeError(ErrorCode::Cancelled, "Thermal solve cancelled.",
                     "Run it again when you are ready.");
}

} // namespace

Result<void> solveRod(const RodProblem& problem, RodWorkspace& ws, FeaSolution& out,
                      const CancelToken* cancel) {
    if (auto valid = problem.validate(); !valid) {
        return valid;
    }
    if (cancel != nullptr && cancel->cancelled()) {
        return cancelledError();
    }

    assemble(problem, ws.system);
    const EliminatedRows rows = applyDirichlet(ws.system, problem.T0, problem.TL);
    if (auto solved = solveThomas(ws.system, ws.scratch, out.T); !solved) {
        return solved;
    }
    Mesh1D{problem.L, problem.elements}.nodes(out.x);

    // Reactions r = K_row·T − f_row of the unconstrained boundary equations: the nodal heat the
    // boundary supplies. Heat leaving the rod is its negative.
    const std::vector<double>& T = out.T;
    const std::size_t N = T.size() - 1;
    const double r0 = rows.first.diag * T[0] + rows.first.upper * T[1] - rows.first.rhs;
    const double rN = rows.last.lower * T[N - 1] + rows.last.diag * T[N] - rows.last.rhs;
    out.heatOutLeft_W = -r0;
    out.heatOutRight_W = -rN;
    return {};
}

Result<FeaSolution> solveRod(const RodProblem& problem, const CancelToken* cancel) {
    RodWorkspace ws;
    FeaSolution out;
    if (auto solved = solveRod(problem, ws, out, cancel); !solved) {
        return std::move(solved).error();
    }
    return out;
}

} // namespace uniforge::thermal
