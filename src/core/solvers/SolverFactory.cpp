#include <string>

#include "core/solvers/LinearSolver.hpp"
#include "core/solvers/amg/Amg.hpp"
#include "core/solvers/direct/Cholesky.hpp"
#include "core/solvers/krylov/Ic0.hpp"
#include "core/solvers/krylov/Pcg.hpp"

namespace uniforge::solvers {

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
    case SolverKind::AmgPcgSp: {
        SolverOptions sp;
        sp.amgSinglePrecision = true;
        return std::unique_ptr<LinearOperatorSolver>(
            std::make_unique<Pcg>(kind, std::make_unique<AmgPreconditioner>(sp)));
    }
    }
    return makeError(ErrorCode::InvalidInput,
                     "Unknown solver kind " + std::to_string(static_cast<int>(kind)) + ".",
                     "This is a programming error in the caller.");
}

} // namespace uniforge::solvers
