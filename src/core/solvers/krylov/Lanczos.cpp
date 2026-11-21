#include "core/solvers/krylov/Lanczos.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace uniforge::solvers {

namespace {

// Number of eigenvalues of T smaller than x (Sturm sequence of the LDLᵀ pivots of T − x·I).
std::size_t countBelow(const std::vector<double>& d, const std::vector<double>& e, double x) {
    std::size_t count = 0;
    double q = 1.0;
    for (std::size_t i = 0; i < d.size(); ++i) {
        const double coupling = i == 0 ? 0.0 : e[i - 1] * e[i - 1] / q;
        q = d[i] - x - coupling;
        if (q == 0.0) {
            q = -std::numeric_limits<double>::min(); // perturb an exact zero pivot
        }
        if (q < 0.0) {
            ++count;
        }
    }
    return count;
}

// The `which`-th smallest eigenvalue (0-based) by bisection on [lo, hi].
double bisect(const std::vector<double>& d, const std::vector<double>& e, std::size_t which,
              double lo, double hi) {
    for (int it = 0; it < 200; ++it) {
        const double mid = 0.5 * (lo + hi);
        if (mid <= lo || mid >= hi) {
            break; // interval is one ulp wide
        }
        if (countBelow(d, e, mid) > which) {
            hi = mid;
        } else {
            lo = mid;
        }
        if (hi - lo <= 1e-15 * std::max(std::fabs(lo), std::fabs(hi))) {
            break;
        }
    }
    return 0.5 * (lo + hi);
}

} // namespace

ExtremeEigenvalues tridiagonalExtremes(const std::vector<double>& d, const std::vector<double>& e) {
    if (d.empty()) {
        return {};
    }
    // Gershgorin interval.
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < d.size(); ++i) {
        const double r =
            (i > 0 ? std::fabs(e[i - 1]) : 0.0) + (i + 1 < d.size() ? std::fabs(e[i]) : 0.0);
        lo = std::min(lo, d[i] - r);
        hi = std::max(hi, d[i] + r);
    }
    const double pad = 1e-12 * std::max(std::fabs(lo), std::fabs(hi)) + 1e-300;
    lo -= pad;
    hi += pad;
    return {bisect(d, e, 0, lo, hi), bisect(d, e, d.size() - 1, lo, hi)};
}

double kappaFromCg(const std::vector<double>& alpha, const std::vector<double>& beta) {
    const std::size_t k = alpha.size();
    if (k == 0 || beta.size() + 1 < k) {
        return 0.0;
    }
    std::vector<double> d(k);
    std::vector<double> e(k - 1);
    for (std::size_t j = 0; j < k; ++j) {
        d[j] = 1.0 / alpha[j] + (j > 0 ? beta[j - 1] / alpha[j - 1] : 0.0);
        if (j + 1 < k) {
            e[j] = std::sqrt(beta[j]) / alpha[j];
        }
    }
    const ExtremeEigenvalues ev = tridiagonalExtremes(d, e);
    if (!(ev.min > 0.0) || !std::isfinite(ev.max)) {
        return 0.0;
    }
    return ev.max / ev.min;
}

} // namespace uniforge::solvers
