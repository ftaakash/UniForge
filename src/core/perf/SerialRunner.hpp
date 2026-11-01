#pragma once

#include <vector>

#include "core/common/CancelToken.hpp"
#include "core/common/Result.hpp"
#include "core/thermal/RodProblem.hpp"
#include "core/thermal/RodSolver.hpp"

namespace uniforge::perf {

// Result of one batch case (spec §5.3): nodal temperatures and the hottest node.
struct CaseResult {
    std::vector<double> T; // °C, N + 1 nodes
    double Tmax = 0.0;     // °C
    double xAtTmax = 0.0;  // m
};

// Sizes out[i].T to cases[i].elements + 1 so a later run writes into existing buffers and
// allocates no result memory (the benchmark calls this outside the timed region).
void prepareResults(const std::vector<thermal::RodProblem>& cases, std::vector<CaseResult>& out);

// Solves one case into `out` with the caller's scratch: the same code path for the serial and
// the parallel runner, which is why their results are bitwise equal. Reuses out.T's storage.
Result<void> solveCase(const thermal::RodProblem& problem, thermal::RodWorkspace& workspace,
                       thermal::FeaSolution& scratch, CaseResult& out);

// Serial reference: every case in index order on the calling thread.
// `out` must come from prepareResults(cases, out). Cancellation is checked before every case.
// On error `out` is partially written and must not be published.
Result<void> runSerialInto(const std::vector<thermal::RodProblem>& cases,
                           std::vector<CaseResult>& out, const CancelToken* cancel = nullptr);

// Allocating form (spec §5.2): out[i] belongs to cases[i]. Errors: a case's validation error,
// Cancelled.
[[nodiscard]] Result<std::vector<CaseResult>> runSerial(
    const std::vector<thermal::RodProblem>& cases, const CancelToken* cancel = nullptr);

} // namespace uniforge::perf
