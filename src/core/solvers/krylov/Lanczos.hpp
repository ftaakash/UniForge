#pragma once

#include <vector>

namespace uniforge::solvers {

// Condition-number estimate from preconditioned CG (build spec §5.3, amendment A3).
//
// CG builds, at no extra cost, the Lanczos tridiagonal T of the preconditioned operator M⁻¹K
// from its step lengths α_j and update ratios β_j = (r_{j+1}ᵀz_{j+1}) / (r_jᵀz_j):
//
//     T_00     = 1/α_0
//     T_jj     = 1/α_j + β_{j−1}/α_{j−1}         (j ≥ 1)
//     T_j,j+1  = sqrt(β_j) / α_j
//
// The extreme eigenvalues of T (Ritz values) approach those of M⁻¹K from inside as CG proceeds,
// so κ_est = λ_max(T)/λ_min(T) is a lower bound that tightens with the iteration count
// (Lanczos–CG connection: Y. Saad, Iterative Methods for Sparse Linear Systems, 2nd ed., SIAM
// 2003, doi 10.1137/1.9780898718003.)

struct ExtremeEigenvalues {
    double min = 0.0;
    double max = 0.0;
};

// Smallest and largest eigenvalue of the symmetric tridiagonal matrix with diagonal `d` and
// off-diagonal `e` (e.size() == d.size() − 1), by Sturm-sequence bisection inside the
// Gershgorin interval (relative accuracy ~1e-14). Empty d gives {0, 0}.
[[nodiscard]] ExtremeEigenvalues tridiagonalExtremes(const std::vector<double>& d,
                                                     const std::vector<double>& e);

// κ_est from the CG coefficients (alpha.size() = k steps, beta.size() ≥ k − 1; extra entries are
// ignored). Returns 0 when there is nothing to estimate (k = 0) or λ_min(T) ≤ 0.
[[nodiscard]] double kappaFromCg(const std::vector<double>& alpha, const std::vector<double>& beta);

} // namespace uniforge::solvers
