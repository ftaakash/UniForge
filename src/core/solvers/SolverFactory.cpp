#include <algorithm>
#include <string>
#include <vector>

#include "core/solvers/LinearSolver.hpp"
#include "core/solvers/amg/Amg.hpp"
#include "core/solvers/direct/Cholesky.hpp"
#include "core/solvers/krylov/Ic0.hpp"
#include "core/solvers/krylov/Pcg.hpp"

namespace uniforge::solvers {

Result<SolveReport> LinearOperatorSolver::solveMany(const std::vector<Vector>& F,
                                                    std::vector<Vector>& X,
                                                    const SolveControl& control) {
    if (F.size() != X.size()) {
        return makeError(ErrorCode::InvalidInput,
                         "solveMany needs one solution vector per load case.",
                         "This is a programming error in the caller.");
    }
    SolveReport total;
    for (std::size_t j = 0; j < F.size(); ++j) {
        auto r = solve(F[j], X[j], control);
        if (!r) {
            return std::move(r).error();
        }
        const SolveReport& s = r.value();
        total.seconds += s.seconds;
        total.observerSeconds += s.observerSeconds;
        total.spmvCount += s.spmvCount;
        total.bytesMoved += s.bytesMoved;
        total.workSpmvEq += s.workSpmvEq;
        total.iterations = std::max(total.iterations, s.iterations);
        total.finalTrueRelResidual = std::max(total.finalTrueRelResidual, s.finalTrueRelResidual);
        if (j == 0) {
            total.residualSum = s.residualSum;
            total.kappaEstimate = s.kappaEstimate;
        }
        if (s.status != SolveStatus::Converged && total.status == SolveStatus::Converged) {
            total.status = s.status;
            total.detail = "load case " + std::to_string(j) + ": " + s.detail;
        }
    }
    return total;
}

Result<std::unique_ptr<LinearOperatorSolver>> makeSolver(SolverKind kind) {
    switch (kind) {
    case SolverKind::PcgJacobi:
        return std::unique_ptr<LinearOperatorSolver>(
            std::make_unique<Pcg>(kind, std::make_unique<JacobiPreconditioner>()));
    case SolverKind::DirectND:
        return std::unique_ptr<LinearOperatorSolver>(
            std::make_unique<DirectCholesky>(kind, Ordering::NestedDissection));
    case SolverKind::DirectRCM:
        return std::unique_ptr<LinearOperatorSolver>(
            std::make_unique<DirectCholesky>(kind, Ordering::Rcm));
    case SolverKind::PcgIc0:
        return std::unique_ptr<LinearOperatorSolver>(
            std::make_unique<Pcg>(kind, std::make_unique<Ic0Preconditioner>()));
    case SolverKind::Amg:
        return std::unique_ptr<LinearOperatorSolver>(std::make_unique<AmgSolver>());
    case SolverKind::AmgPcg:
        return std::unique_ptr<LinearOperatorSolver>(
            std::make_unique<Pcg>(kind, std::make_unique<AmgPreconditioner>()));
    case SolverKind::AmgPcgSp:
    case SolverKind::AmgPcgCheb:
    case SolverKind::AmgPcgChebSp: {
        SolverOptions o;
        o.amgSinglePrecision = kind != SolverKind::AmgPcgCheb;
        o.amgSmoother =
            kind == SolverKind::AmgPcgSp ? AmgSmoother::GaussSeidel : AmgSmoother::Chebyshev;
        return std::unique_ptr<LinearOperatorSolver>(
            std::make_unique<Pcg>(kind, std::make_unique<AmgPreconditioner>(o)));
    }
    case SolverKind::GmgPcg:
    case SolverKind::GmgPcgChebSp: {
        SolverOptions o;
        o.coarsening = Coarsening::Geometric;
        if (kind == SolverKind::GmgPcgChebSp) {
            o.amgSinglePrecision = true;
            o.amgSmoother = AmgSmoother::Chebyshev;
        }
        return std::unique_ptr<LinearOperatorSolver>(
            std::make_unique<Pcg>(kind, std::make_unique<AmgPreconditioner>(o)));
    }
    case SolverKind::AmgPcgChebSpBatch:
        return std::unique_ptr<LinearOperatorSolver>(std::make_unique<AmgBatchPcg>());
    }
    return makeError(ErrorCode::InvalidInput,
                     "Unknown solver kind " + std::to_string(static_cast<int>(kind)) + ".",
                     "This is a programming error in the caller.");
}

} // namespace uniforge::solvers
