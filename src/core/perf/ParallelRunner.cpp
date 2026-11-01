#include "core/perf/ParallelRunner.hpp"

#include <string>
#include <utility>

#include "core/perf/WorkerPool.hpp"

namespace uniforge::perf {

Result<void> runParallelInto(const std::vector<thermal::RodProblem>& cases, int workers,
                             std::vector<CaseResult>& out, const CancelToken* cancel) {
    if (workers < 1) {
        return makeError(ErrorCode::InvalidInput,
                         "Worker count must be at least 1 (got " + std::to_string(workers) + ").",
                         "Choose 1 or more workers.");
    }
    if (out.size() != cases.size()) {
        return makeError(ErrorCode::Internal, "Result buffer does not match the batch size.",
                         "Call prepareResults() before running the batch.");
    }
    const WorkerPool pool(workers);
    const auto threads = static_cast<std::size_t>(pool.threadsFor(cases.size()));
    std::vector<thermal::RodWorkspace> workspaces(threads);
    std::vector<thermal::FeaSolution> scratch(threads);
    return pool.run(
        cases.size(),
        [&](std::size_t i, int worker) {
            const auto w = static_cast<std::size_t>(worker);
            return solveCase(cases[i], workspaces[w], scratch[w], out[i]);
        },
        cancel);
}

Result<std::vector<CaseResult>> runParallel(const std::vector<thermal::RodProblem>& cases,
                                            int workers, const CancelToken* cancel) {
    std::vector<CaseResult> out;
    prepareResults(cases, out);
    if (auto r = runParallelInto(cases, workers, out, cancel); !r) {
        return std::move(r).error();
    }
    return out;
}

} // namespace uniforge::perf
