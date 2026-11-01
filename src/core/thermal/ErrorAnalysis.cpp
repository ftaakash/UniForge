#include "core/thermal/ErrorAnalysis.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "core/thermal/Analytical.hpp"

namespace uniforge::thermal {

Result<ErrorReport> analyse(const RodProblem& problem, const FeaSolution& solution) {
    const auto nodes = static_cast<std::size_t>(problem.elements) + 1U;
    if (problem.elements < 1 || solution.x.size() != nodes || solution.T.size() != nodes) {
        return makeError(ErrorCode::InvalidInput,
                         "FE solution has " + std::to_string(solution.T.size()) +
                             " nodes but the problem has N = " +
                             std::to_string(problem.elements) + " elements.",
                         "Analyse the solution produced for this problem.");
    }

    ErrorReport r;
    r.nodalExact.resize(nodes);
    for (std::size_t i = 0; i < nodes; ++i) {
        r.nodalExact[i] = exactTemperature(problem, solution.x[i]);
        r.maxNodalErr = std::max(r.maxNodalErr, std::abs(solution.T[i] - r.nodalExact[i]));
    }

    const std::size_t elements = nodes - 1;
    r.midX.resize(elements);
    r.midFe.resize(elements);
    r.midExact.resize(elements);
    for (std::size_t e = 0; e < elements; ++e) {
        r.midX[e] = 0.5 * (solution.x[e] + solution.x[e + 1]);
        r.midFe[e] = 0.5 * (solution.T[e] + solution.T[e + 1]);
        r.midExact[e] = exactTemperature(problem, r.midX[e]);
        r.maxMidErr = std::max(r.maxMidErr, std::abs(r.midFe[e] - r.midExact[e]));
    }

    const double generated = problem.q * problem.A * problem.L;
    r.energyResidual_W = solution.heatOutLeft_W + solution.heatOutRight_W - generated;
    return r;
}

NodalPeak nodalPeak(const FeaSolution& solution) noexcept {
    NodalPeak peak;
    if (solution.T.empty()) {
        return peak;
    }
    const auto it = std::max_element(solution.T.begin(), solution.T.end());
    peak.node = static_cast<std::size_t>(it - solution.T.begin());
    peak.x = solution.x[peak.node];
    peak.T = *it;
    return peak;
}

} // namespace uniforge::thermal
