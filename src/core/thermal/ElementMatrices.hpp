#pragma once

#include <array>

namespace uniforge::thermal {

using Matrix2 = std::array<std::array<double, 2>, 2>;
using Vector2 = std::array<double, 2>;

// Two-node linear element of length h (spec §7.3), from the weak form
//     ∫ k A (dw/dx)(dT/dx) dx = ∫ w q A dx   with  T ≈ N1 T1 + N2 T2,  N1 = 1 − ξ, N2 = ξ.
// Stiffness: Ke = (kA/h)·[[1, −1], [−1, 1]]   (units W/K)
[[nodiscard]] inline Matrix2 elementStiffness(double k, double A, double h) noexcept {
    const double s = k * A / h;
    return {{{s, -s}, {-s, s}}};
}

// Consistent load for uniform q: fe = ∫ N q A dx = (qAh/2)·[1, 1]   (units W)
[[nodiscard]] inline Vector2 elementLoad(double q, double A, double h) noexcept {
    const double f = q * A * h / 2.0;
    return {f, f};
}

} // namespace uniforge::thermal
