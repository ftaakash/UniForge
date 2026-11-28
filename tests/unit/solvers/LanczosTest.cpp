// Sturm-bisection eigenvalues of symmetric tridiagonal matrices (κ estimate, amendment A3).

#include "core/solvers/krylov/Lanczos.hpp"

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

using namespace uniforge::solvers;

TEST(Lanczos, TridiagonalExtremesOfTheDiscreteLaplacian) {
    // tridiag(−1, 2, −1), n = 50: eigenvalues 2 − 2cos(kπ/51).
    const std::size_t n = 50;
    const std::vector<double> d(n, 2.0);
    const std::vector<double> e(n - 1, -1.0);
    const ExtremeEigenvalues ev = tridiagonalExtremes(d, e);
    const double pi = std::acos(-1.0);
    EXPECT_NEAR(ev.min, 2.0 - 2.0 * std::cos(pi / 51.0), 1e-13);
    EXPECT_NEAR(ev.max, 2.0 - 2.0 * std::cos(50.0 * pi / 51.0), 1e-13);
}

TEST(Lanczos, DiagonalAndSingleEntryCases) {
    const ExtremeEigenvalues one = tridiagonalExtremes({3.5}, {});
    EXPECT_NEAR(one.min, 3.5, 1e-14);
    EXPECT_NEAR(one.max, 3.5, 1e-14);
    const ExtremeEigenvalues diag = tridiagonalExtremes({4.0, -1.0, 7.0}, {0.0, 0.0});
    EXPECT_NEAR(diag.min, -1.0, 1e-14);
    EXPECT_NEAR(diag.max, 7.0, 1e-14);
    const ExtremeEigenvalues empty = tridiagonalExtremes({}, {});
    EXPECT_EQ(empty.min, 0.0);
    EXPECT_EQ(empty.max, 0.0);
}

TEST(Lanczos, KappaFromCgNeedsCoefficients) {
    EXPECT_EQ(kappaFromCg({}, {}), 0.0);
    // One step: T = [1/α], κ = 1.
    EXPECT_NEAR(kappaFromCg({0.5}, {}), 1.0, 1e-14);
}
