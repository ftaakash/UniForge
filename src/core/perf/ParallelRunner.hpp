#pragma once

#include <vector>

#include "core/common/CancelToken.hpp"
#include "core/common/Result.hpp"
#include "core/perf/SerialRunner.hpp"
#include "core/thermal/RodProblem.hpp"

namespace uniforge::perf {

// Parallel batch on a WorkerPool of `workers` threads. Each worker owns its solver scratch and
// writes only out[i] for the indices it takes, so out[i] still belongs to cases[i] (index
// order preserved) and every value is bitwise equal to runSerial's: same solveCase, same
// inputs, no reductions across cases.
// `out` must come from prepareResults(cases, out). On error `out` must not be published.
// Errors: InvalidInput when workers < 1; a case's validation error; Cancelled; Internal for a
// worker exception.
Result<void> runParallelInto(const std::vector<thermal::RodProblem>& cases, int workers,
                             std::vector<CaseResult>& out, const CancelToken* cancel = nullptr);

// Allocating form (spec §5.2).
[[nodiscard]] Result<std::vector<CaseResult>> runParallel(
    const std::vector<thermal::RodProblem>& cases, int workers,
    const CancelToken* cancel = nullptr);

} // namespace uniforge::perf
