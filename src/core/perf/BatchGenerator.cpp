#include "core/perf/BatchGenerator.hpp"

#include <string>

#include "core/common/Rng.hpp"

namespace uniforge::perf {

Result<std::vector<thermal::RodProblem>> generateBatch(std::uint64_t seed, int cases,
                                                       int elements) {
    if (cases < 1 || cases > kMaxBatchCases) {
        return makeError(ErrorCode::InvalidInput,
                         "Number of cases must be between 1 and " +
                             std::to_string(kMaxBatchCases) + " (got " + std::to_string(cases) +
                             ").",
                         "Choose a case count in that range.");
    }
    if (elements < 1 || elements > thermal::RodProblem::kMaxElements) {
        return makeError(ErrorCode::InvalidInput,
                         "Elements per case must be between 1 and " +
                             std::to_string(thermal::RodProblem::kMaxElements) + " (got " +
                             std::to_string(elements) + ").",
                         "Choose an element count in that range.");
    }

    Rng rng(seed);
    std::vector<thermal::RodProblem> batch(static_cast<std::size_t>(cases));
    for (thermal::RodProblem& p : batch) {
        p.k = rng.uniform(kBatchKMin, kBatchKMax);
        p.q = rng.uniform(kBatchQMin, kBatchQMax);
        p.elements = elements;
    }
    return batch;
}

} // namespace uniforge::perf
