#pragma once

#include "core/common/Result.hpp"

namespace uniforge::thermal {

// Steady 1D heat conduction in a rod with uniform internal heat generation (Task C):
//
//     -d/dx( k A dT/dx ) = q A   on 0 < x < L,   T(0) = T0,   T(L) = TL
//
// Assumptions: steady state, 1D (temperature uniform over the cross-section), constant k and A,
// insulated lateral surface, uniform q. Defaults are the brief's values.
struct RodProblem {
    double L = 1.0;    // rod length, m
    double A = 1e-4;   // cross-section area, m²
    double k = 10.0;   // thermal conductivity, W/(m·K)
    double q = 1e4;    // volumetric heat generation, W/m³
    double T0 = 100.0; // temperature at x = 0, °C
    double TL = 20.0;  // temperature at x = L, °C
    int elements = 10; // number of two-node linear elements N (N + 1 nodes)

    static constexpr int kMaxElements = 10'000'000;

    // L, A, k > 0 and finite; q, T0, TL finite; 1 <= elements <= kMaxElements.
    // Errors: NonFinite (a NaN/inf parameter), InvalidInput (a value out of range),
    // LimitExceeded (more than kMaxElements). The message names the parameter and its unit.
    [[nodiscard]] Result<void> validate() const;
};

} // namespace uniforge::thermal
