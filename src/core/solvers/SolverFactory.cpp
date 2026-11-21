#include <string>

#include "core/solvers/LinearSolver.hpp"
#include "core/solvers/krylov/Pcg.hpp"

namespace uniforge::solvers {

Result<std::unique_ptr<LinearOperatorSolver>> makeSolver(SolverKind kind) {
    switch (kind) {
    case SolverKind::PcgJacobi:
        return std::unique_ptr<LinearOperatorSolver>(
            std::make_unique<Pcg>(kind, std::make_unique<JacobiPreconditioner>()));
    case SolverKind::DirectND:
    case SolverKind::DirectRCM:
    case SolverKind::PcgIc0:
    case SolverKind::Amg:
    case SolverKind::AmgPcg:
        break;
    }
    const char* phase = kind == SolverKind::PcgIc0                                ? "V2-6"
                        : (kind == SolverKind::Amg || kind == SolverKind::AmgPcg) ? "V2-7"
                                                                                  : "V2-5";
    return makeError(ErrorCode::InvalidInput,
                     std::string("Solver ") + std::string(label(kind)) +
                         " is not built yet; it is added in phase " + phase + ".",
                     "Use PCG-J until then.");
}

} // namespace uniforge::solvers
