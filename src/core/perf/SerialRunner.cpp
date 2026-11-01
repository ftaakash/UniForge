#include "core/perf/SerialRunner.hpp"

#include <utility>

namespace uniforge::perf {

namespace {

Error cancelledError() {
    return makeError(ErrorCode::Cancelled, "Batch run cancelled.",
                     "No results were published; run it again when you are ready.");
}

} // namespace

void prepareResults(const std::vector<thermal::RodProblem>& cases, std::vector<CaseResult>& out) {
    out.resize(cases.size());
    for (std::size_t i = 0; i < cases.size(); ++i) {
        out[i].T.resize(static_cast<std::size_t>(cases[i].elements) + 1);
    }
}

Result<void> solveCase(const thermal::RodProblem& problem, thermal::RodWorkspace& workspace,
                       thermal::FeaSolution& scratch, CaseResult& out) {
    // Lend out.T to the solver: it resizes to the same length, so nothing is allocated.
    std::swap(scratch.T, out.T);
    auto solved = thermal::solveRod(problem, workspace, scratch);
    std::swap(scratch.T, out.T);
    if (!solved) {
        return solved;
    }
    std::size_t hottest = 0;
    for (std::size_t i = 1; i < out.T.size(); ++i) {
        if (out.T[i] > out.T[hottest]) {
            hottest = i;
        }
    }
    out.Tmax = out.T[hottest];
    out.xAtTmax = scratch.x[hottest];
    return {};
}

Result<void> runSerialInto(const std::vector<thermal::RodProblem>& cases,
                           std::vector<CaseResult>& out, const CancelToken* cancel) {
    if (out.size() != cases.size()) {
        return makeError(ErrorCode::Internal, "Result buffer does not match the batch size.",
                         "Call prepareResults() before running the batch.");
    }
    thermal::RodWorkspace workspace;
    thermal::FeaSolution scratch;
    for (std::size_t i = 0; i < cases.size(); ++i) {
        if (cancel != nullptr && cancel->cancelled()) {
            return cancelledError();
        }
        if (auto r = solveCase(cases[i], workspace, scratch, out[i]); !r) {
            return r;
        }
    }
    return {};
}

Result<std::vector<CaseResult>> runSerial(const std::vector<thermal::RodProblem>& cases,
                                          const CancelToken* cancel) {
    std::vector<CaseResult> out;
    prepareResults(cases, out);
    if (auto r = runSerialInto(cases, out, cancel); !r) {
        return std::move(r).error();
    }
    return out;
}

} // namespace uniforge::perf
