// Convergence of the midpoint error (spec §7.3, N5): max midpoint error = q·h²/(8k) to 1e-9,
// ratio 4 per halving of h, observed order 2.

#include "core/thermal/ConvergenceStudy.hpp"

#include <cmath>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/thermal/Analytical.hpp"

using namespace uniforge::thermal;

TEST(ConvergenceStudy, MidpointErrorsMatchQH2Over8K) {
    std::vector<StudyMesh> meshes;
    const auto rows = convergenceStudy(RodProblem{}, {10, 20, 40, 80}, {}, nullptr, &meshes);
    ASSERT_TRUE(rows.ok()) << rows.error().message;
    ASSERT_EQ(rows.value().size(), 4U);
    ASSERT_EQ(meshes.size(), 4U);

    const double expected[] = {1.25, 0.3125, 0.078125, 0.01953125}; // °C
    for (std::size_t i = 0; i < 4; ++i) {
        const StudyRow& r = rows.value()[i];
        EXPECT_NEAR(r.maxMidErr, expected[i], 1e-9) << "N = " << r.N;
        EXPECT_NEAR(r.expected, expected[i], 1e-15) << "N = " << r.N;
        EXPECT_LE(r.maxNodalErr, 1e-10);
        EXPECT_LE(std::abs(r.energyResidual_W), 1e-10);
    }
}

TEST(ConvergenceStudy, EveryMidpointHasTheSameError) {
    // Nodal values are exact, so each element's error is the parabola's sag over that element,
    // the same for all elements: the maximum is reached everywhere, not at one lucky point.
    std::vector<StudyMesh> meshes;
    ASSERT_TRUE(convergenceStudy(RodProblem{}, {10, 80}, {}, nullptr, &meshes).ok());
    const double expected[] = {1.25, 0.01953125};
    for (std::size_t m = 0; m < meshes.size(); ++m) {
        const ErrorReport& r = meshes[m].report;
        ASSERT_EQ(r.midX.size(), meshes[m].solution.T.size() - 1);
        for (std::size_t e = 0; e < r.midX.size(); ++e) {
            EXPECT_NEAR(r.midExact[e] - r.midFe[e], expected[m], 1e-9)
                << "FE interpolation lies below the exact parabola; element " << e;
        }
    }
}

TEST(ConvergenceStudy, RatioIsFourAndOrderIsTwo) {
    const auto rows = convergenceStudy(RodProblem{}, {10, 20, 40, 80});
    ASSERT_TRUE(rows.ok());
    EXPECT_TRUE(std::isnan(rows.value()[0].ratioToPrev)) << "no previous mesh for row 1";
    EXPECT_TRUE(std::isnan(rows.value()[0].order));
    for (std::size_t i = 1; i < rows.value().size(); ++i) {
        EXPECT_NEAR(rows.value()[i].ratioToPrev, 4.0, 1e-6) << "N = " << rows.value()[i].N;
        EXPECT_NEAR(rows.value()[i].order, 2.0, 1e-6) << "N = " << rows.value()[i].N;
    }
}

TEST(ConvergenceStudy, ReportsProgressPerMesh) {
    std::vector<double> fractions;
    std::vector<std::string> stages;
    const auto rows = convergenceStudy(RodProblem{}, {10, 20, 40, 80},
                                       [&](double f, std::string_view stage) {
                                           fractions.push_back(f);
                                           stages.emplace_back(stage);
                                       });
    ASSERT_TRUE(rows.ok());
    ASSERT_EQ(fractions.size(), 4U);
    EXPECT_DOUBLE_EQ(fractions.front(), 0.25);
    EXPECT_DOUBLE_EQ(fractions.back(), 1.0);
    EXPECT_EQ(stages[2], "N = 40 (3/4)");
}

TEST(ConvergenceStudy, HoldsForOtherParameters) {
    // The q·h²/(8k) law and nodal exactness are not special to the brief's numbers.
    RodProblem p;
    p.L = 0.3;
    p.A = 2e-5;
    p.k = 237.0;
    p.q = 3.7e6;
    p.T0 = -5.0;
    p.TL = 60.0;
    const auto rows = convergenceStudy(p, {7, 14, 28});
    ASSERT_TRUE(rows.ok());
    for (const StudyRow& r : rows.value()) {
        EXPECT_NEAR(r.maxMidErr, expectedMidpointError(p, r.h), 1e-9) << "N = " << r.N;
        EXPECT_LE(r.maxNodalErr, 1e-10);
        EXPECT_LE(std::abs(r.energyResidual_W), 1e-10);
    }
    EXPECT_NEAR(rows.value()[2].order, 2.0, 1e-6);
}
