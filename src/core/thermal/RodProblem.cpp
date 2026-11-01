#include "core/thermal/RodProblem.hpp"

#include <cmath>
#include <string>

namespace uniforge::thermal {

namespace {

struct Param {
    const char* name;
    const char* unit;
    double value;
    bool mustBePositive;
};

} // namespace

Result<void> RodProblem::validate() const {
    const Param params[] = {
        {"Length L", "m", L, true},
        {"Cross-section area A", "m²", A, true},
        {"Conductivity k", "W/(m·K)", k, true},
        {"Heat generation q", "W/m³", q, false},
        {"Left temperature T0", "°C", T0, false},
        {"Right temperature TL", "°C", TL, false},
    };
    for (const Param& p : params) {
        if (!std::isfinite(p.value)) {
            return makeError(ErrorCode::NonFinite,
                             std::string(p.name) + " (" + p.unit + ") is not a finite number.",
                             "Enter a finite value.");
        }
        if (p.mustBePositive && !(p.value > 0.0)) {
            return makeError(ErrorCode::InvalidInput,
                             std::string(p.name) + " (" + p.unit + ") must be greater than 0.",
                             "Enter a positive value.");
        }
    }
    if (elements < 1) {
        return makeError(ErrorCode::InvalidInput,
                         "Number of elements N = " + std::to_string(elements) +
                             " must be at least 1.",
                         "Enter an integer N >= 1, e.g. 10.");
    }
    if (elements > kMaxElements) {
        return makeError(ErrorCode::LimitExceeded,
                         "Number of elements N = " + std::to_string(elements) +
                             " exceeds the limit of " + std::to_string(kMaxElements) + ".",
                         "Use at most 10,000,000 elements.");
    }
    return {};
}

} // namespace uniforge::thermal
