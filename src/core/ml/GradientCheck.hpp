#pragma once

#include <array>

#include "core/ml/TinyNet.hpp"

namespace uniforge::ml {

// Brief's finite-difference settings (Task F, F2).
inline constexpr double kFdEpsilon = 1e-6;   // central-difference step
inline constexpr double kFdTolerance = 1e-5; // pass if |analytic − numeric| <= this

struct GradCheck {
    Grads analytic, numeric, absDiff;
    std::array<bool, kParamCount> passEach{}; // per parameter, order w, b, v, c
    bool pass = false;                        // all four pass
    double eps = kFdEpsilon;
    double tolerance = kFdTolerance;
};

// Checks backward() against central differences, one parameter at a time:
//     numeric_i = (J(p + ε·e_i) − J(p − ε·e_i)) / (2ε)
// Truncation error is O(ε²) ≈ 1e-12 and round-off about 1e-16·J/ε ≈ 1e-10 here, both far
// below the 1e-5 tolerance, so a failing parameter means a wrong analytic formula.
[[nodiscard]] GradCheck centralDiff(const Params& p, double x, double y,
                                    double eps = kFdEpsilon, double tolerance = kFdTolerance);

} // namespace uniforge::ml
