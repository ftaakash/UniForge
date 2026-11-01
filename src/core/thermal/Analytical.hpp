#pragma once

#include "core/thermal/RodProblem.hpp"

namespace uniforge::thermal {

// Exact solution of −k T'' = q with T(0) = T0, T(L) = TL (A cancels for constant A):
//     T(x) = T0 + (TL − T0)·x/L + (q / 2k)·x·(L − x)
// a straight line between the end temperatures plus a parabola that vanishes at both ends.
// Brief values: T(x) = 100 + 420x − 500x² °C.
[[nodiscard]] double exactTemperature(const RodProblem& p, double x) noexcept;

struct TemperaturePeak {
    double x = 0.0; // m
    double T = 0.0; // °C
};

// Hottest point of the exact profile. For q > 0 the parabola peaks where T'(x) = 0,
//     x* = L/2 + k(TL − T0)/(qL)
// (brief values: x* = 0.42 m, T = 188.2 °C); when x* falls outside [0, L], or q <= 0, the
// maximum is at the hotter end.
[[nodiscard]] TemperaturePeak exactPeak(const RodProblem& p) noexcept;

// Largest interpolation error of linear elements of length h for this problem, reached at every
// element midpoint: |q|·h² / (8k). Derivation in docs/thermal_fea.md.
[[nodiscard]] double expectedMidpointError(const RodProblem& p, double h) noexcept;

} // namespace uniforge::thermal
