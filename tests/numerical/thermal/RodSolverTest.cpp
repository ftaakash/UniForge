// Numerical verification of the rod solver against the exact solution (spec §7.3, N5):
// exact BCs, nodal exactness, energy balance, T_max, and Thomas vs a dense oracle.

#include "core/thermal/RodSolver.hpp"

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "core/thermal/Analytical.hpp"
#include "core/thermal/DirichletBC.hpp"
#include "core/thermal/ErrorAnalysis.hpp"
#include "core/thermal/ThomasSolver.hpp"
#include "support/DenseGauss.hpp"

using namespace uniforge::thermal;

namespace {

RodProblem briefProblem(int elements) {
    RodProblem p;
    p.elements = elements;
    return p;
}

} // namespace

class RodSolverMeshes : public ::testing::TestWithParam<int> {};

TEST_P(RodSolverMeshes, BoundaryValuesAreExact) {
    const auto sol = solveRod(briefProblem(GetParam()));
    ASSERT_TRUE(sol.ok()) << sol.error().message;
    const auto& s = sol.value();
    ASSERT_EQ(s.x.size(), static_cast<std::size_t>(GetParam()) + 1U);
    ASSERT_EQ(s.T.size(), s.x.size());
    EXPECT_EQ(s.T.front(), 100.0);
    EXPECT_EQ(s.T.back(), 20.0);
    EXPECT_EQ(s.x.front(), 0.0);
    EXPECT_EQ(s.x.back(), 1.0);
}

TEST_P(RodSolverMeshes, NodalValuesMatchExactSolution) {
    const RodProblem p = briefProblem(GetParam());
    const auto sol = solveRod(p);
    ASSERT_TRUE(sol.ok());
    const auto report = analyse(p, sol.value());
    ASSERT_TRUE(report.ok());
    EXPECT_LE(report.value().maxNodalErr, 1e-10);
}

TEST_P(RodSolverMeshes, EndHeatFlowsBalanceGeneratedHeat) {
    const RodProblem p = briefProblem(GetParam());
    const auto sol = solveRod(p);
    ASSERT_TRUE(sol.ok());
    const auto& s = sol.value();
    // Exact: k·A·T'(0) = 0.42 W leaves on the left, −k·A·T'(L) = 0.58 W on the right.
    EXPECT_NEAR(s.heatOutLeft_W, 0.42, 1e-10);
    EXPECT_NEAR(s.heatOutRight_W, 0.58, 1e-10);
    const auto report = analyse(p, s);
    ASSERT_TRUE(report.ok());
    EXPECT_LE(std::abs(report.value().energyResidual_W), 1e-10) << "q·A·L = 1 W";
}

TEST_P(RodSolverMeshes, ThomasMatchesDenseGaussianElimination) {
    const RodProblem p = briefProblem(GetParam());
    TridiagonalSystem s;
    assemble(p, s);
    applyDirichlet(s, p.T0, p.TL);
    std::vector<double> scratch;
    std::vector<double> thomas;
    ASSERT_TRUE(solveThomas(s, scratch, thomas).ok());
    const std::vector<double> dense = uniforge::testing::denseSolve(uniforge::testing::toDense(s), s.f);
    ASSERT_EQ(dense.size(), thomas.size());
    for (std::size_t i = 0; i < dense.size(); ++i) {
        EXPECT_NEAR(thomas[i], dense[i], 1e-11) << "node " << i;
    }
}

INSTANTIATE_TEST_SUITE_P(BriefMeshes, RodSolverMeshes, ::testing::Values(1, 2, 10, 20, 40, 80),
                         [](const auto& paramInfo) {
                             return "N" + std::to_string(paramInfo.param);
                         });

TEST(RodSolver, PeakIs188Point2AtX0Point42) {
    const TemperaturePeak exact = exactPeak(briefProblem(10));
    EXPECT_NEAR(exact.x, 0.42, 1e-12);
    EXPECT_NEAR(exact.T, 188.2, 1e-12);

    for (const int n : {50, 100}) { // both meshes have a node at x = 0.42 m
        const auto sol = solveRod(briefProblem(n));
        ASSERT_TRUE(sol.ok());
        const NodalPeak peak = nodalPeak(sol.value());
        EXPECT_EQ(peak.node, static_cast<std::size_t>(n * 42 / 100)) << "N = " << n;
        EXPECT_EQ(peak.x, 0.42) << "N = " << n;
        EXPECT_NEAR(peak.T, 188.2, 1e-10) << "N = " << n;
    }
}

TEST(RodSolver, WorkspaceReuseGivesBitwiseIdenticalResults) {
    RodWorkspace ws;
    FeaSolution reused;
    for (const int n : {80, 10, 40}) { // shrink and grow the buffers
        ASSERT_TRUE(solveRod(briefProblem(n), ws, reused).ok());
        const auto fresh = solveRod(briefProblem(n));
        ASSERT_TRUE(fresh.ok());
        EXPECT_EQ(reused.T, fresh.value().T);
        EXPECT_EQ(reused.x, fresh.value().x);
        EXPECT_EQ(reused.heatOutLeft_W, fresh.value().heatOutLeft_W);
    }
}

TEST(RodSolver, NoHeatGenerationGivesStraightLine) {
    RodProblem p = briefProblem(8);
    p.q = 0.0;
    const auto sol = solveRod(p);
    ASSERT_TRUE(sol.ok());
    for (std::size_t i = 0; i < sol.value().T.size(); ++i) {
        EXPECT_NEAR(sol.value().T[i], 100.0 - 80.0 * sol.value().x[i], 1e-12);
    }
    // Heat flows through: k·A·ΔT/L = 1e-3 · 80 = 0.08 W enters left and leaves right.
    EXPECT_NEAR(sol.value().heatOutLeft_W, -0.08, 1e-14);
    EXPECT_NEAR(sol.value().heatOutRight_W, 0.08, 1e-14);
}

TEST(ThomasSolver, MatchesDenseOracleOnRandomDominantSystems) {
    std::mt19937_64 rng(20261101);
    std::uniform_real_distribution<double> u(-1.0, 1.0); // distribution choice only affects the test data
    for (int trial = 0; trial < 20; ++trial) {
        const std::size_t n = 2 + static_cast<std::size_t>(trial) * 3;
        TridiagonalSystem s;
        s.reset(n);
        for (std::size_t i = 0; i < n; ++i) {
            s.a[i] = i > 0 ? u(rng) : 0.0;
            s.c[i] = i + 1 < n ? u(rng) : 0.0;
            s.b[i] = std::abs(s.a[i]) + std::abs(s.c[i]) + 0.5 + std::abs(u(rng));
            s.f[i] = 10.0 * u(rng);
        }
        std::vector<double> scratch;
        std::vector<double> x;
        ASSERT_TRUE(solveThomas(s, scratch, x).ok());
        const auto dense = uniforge::testing::denseSolve(uniforge::testing::toDense(s), s.f);
        ASSERT_EQ(dense.size(), n);
        for (std::size_t i = 0; i < n; ++i) {
            EXPECT_NEAR(x[i], dense[i], 1e-12 * (1.0 + std::abs(dense[i])))
                << "trial " << trial << " row " << i;
        }
    }
}
