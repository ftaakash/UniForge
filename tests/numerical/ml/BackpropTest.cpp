// Numerical verification of the Task F one-neuron network against the brief (spec §7.6, F2):
// forward values, hand-derived gradients, central-difference check, one simultaneous update and
// the 100-step loss curve. The brief quotes 6 decimals, so values are compared to 1e-6.

#include "core/ml/GradientCheck.hpp"
#include "core/ml/TinyNet.hpp"
#include "core/ml/Trainer.hpp"

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

using namespace uniforge::ml;

namespace {

constexpr double kBriefTol = 1e-6;

const BackpropProblem kBrief; // x = 1, y = 2, w = 0.5, b = 0, v = 1, c = 0, lr = 0.1

} // namespace

TEST(BackpropForward, MatchesBrief) {
    const Forward f = forward(kBrief.init, kBrief.x, kBrief.y);
    EXPECT_NEAR(f.z, 0.5, 1e-15);
    EXPECT_NEAR(f.h, 0.462117, kBriefTol);
    EXPECT_NEAR(f.yhat, 0.462117, kBriefTol);
    EXPECT_NEAR(f.err, -1.537883, kBriefTol);
    EXPECT_NEAR(f.loss, 1.182542, kBriefTol);
    EXPECT_DOUBLE_EQ(f.h, std::tanh(0.5));
}

TEST(BackpropBackward, GradientsMatchBrief) {
    const Grads g = backward(kBrief.init, kBrief.x, kBrief.y);
    EXPECT_NEAR(g.dw, -1.209464, kBriefTol);
    EXPECT_NEAR(g.db, -1.209464, kBriefTol);
    EXPECT_NEAR(g.dv, -0.710682, kBriefTol);
    EXPECT_NEAR(g.dc, -1.537883, kBriefTol);
    EXPECT_EQ(g.dw, g.db); // x = 1, so dJ/dw = dJ/db · x is the same number
}

TEST(BackpropBackward, AllGradientsNegativeAtBriefPoint) {
    // ŷ < y, so increasing any parameter raises ŷ toward y: descent increases all four.
    const Grads g = backward(kBrief.init, kBrief.x, kBrief.y);
    EXPECT_LT(g.dw, 0.0);
    EXPECT_LT(g.db, 0.0);
    EXPECT_LT(g.dv, 0.0);
    EXPECT_LT(g.dc, 0.0);
}

TEST(BackpropGradCheck, AllFourParametersPassCentralDifferences) {
    const GradCheck check = centralDiff(kBrief.init, kBrief.x, kBrief.y);
    EXPECT_EQ(check.eps, 1e-6);
    EXPECT_EQ(check.tolerance, 1e-5);
    for (int i = 0; i < kParamCount; ++i) {
        EXPECT_TRUE(check.passEach[static_cast<std::size_t>(i)]) << kParamNames[i];
        EXPECT_LE(get(check.absDiff, i), 1e-5) << kParamNames[i];
        // Much tighter in practice: O(ε²) truncation plus ~1e-10 round-off.
        EXPECT_LE(get(check.absDiff, i), 1e-8) << kParamNames[i];
    }
    EXPECT_TRUE(check.pass);
}

TEST(BackpropGradCheck, PassesAwayFromTheBriefPoint) {
    // A generic point with v ≠ 1 and x ≠ 1 catches formulas that are only right by coincidence
    // (e.g. forgetting the factor v or x).
    const Params p{-0.7, 0.3, 2.5, -0.4};
    const GradCheck check = centralDiff(p, -1.3, 0.8);
    EXPECT_TRUE(check.pass);
    for (int i = 0; i < kParamCount; ++i) {
        EXPECT_LE(get(check.absDiff, i), 1e-8) << kParamNames[i];
    }
}

TEST(BackpropGradCheck, DetectsAWrongGradient) {
    // The check must be able to fail: a tolerance below the round-off floor rejects at least
    // one parameter, and NaN inputs fail every parameter instead of passing silently.
    const GradCheck tight = centralDiff(kBrief.init, kBrief.x, kBrief.y, 1e-6, 0.0);
    EXPECT_FALSE(tight.pass);
    const Params bad{std::nan(""), 0.0, 1.0, 0.0};
    const GradCheck nanCheck = centralDiff(bad, kBrief.x, kBrief.y);
    EXPECT_FALSE(nanCheck.pass);
    for (bool p : nanCheck.passEach) {
        EXPECT_FALSE(p);
    }
}

TEST(BackpropUpdate, OneSimultaneousStepMatchesBrief) {
    const Grads g = backward(kBrief.init, kBrief.x, kBrief.y);
    const Params next = sgdStep(kBrief.init, g, kBrief.lr);
    EXPECT_NEAR(next.w, 0.620946, kBriefTol);
    EXPECT_NEAR(next.b, 0.120946, kBriefTol);
    EXPECT_NEAR(next.v, 1.071068, kBriefTol);
    EXPECT_NEAR(next.c, 0.153788, kBriefTol);
}

TEST(BackpropUpdate, UsesGradientsOfTheOriginalParameters) {
    // A sequential update (v changed before db is formed) would give a different b.
    const Params p = kBrief.init;
    const Grads g = backward(p, kBrief.x, kBrief.y);
    const Params next = sgdStep(p, g, kBrief.lr);
    EXPECT_EQ(next.b, p.b - kBrief.lr * g.db);
    Params sequential = p;
    sequential.v -= kBrief.lr * g.dv;
    const double dbSequential = backward(sequential, kBrief.x, kBrief.y).db;
    EXPECT_GT(std::abs(dbSequential - g.db), 1e-3);
}

TEST(BackpropTrain, ReturnsStepsPlusOneLossesStartingAtInitialLoss) {
    Params last;
    const std::vector<double> losses =
        train(kBrief.init, kBrief.x, kBrief.y, kBrief.lr, kBrief.steps, &last);
    ASSERT_EQ(losses.size(), 101U);
    EXPECT_EQ(losses[0], loss(kBrief.init, kBrief.x, kBrief.y));
    const Params one = sgdStep(kBrief.init, backward(kBrief.init, kBrief.x, kBrief.y), kBrief.lr);
    EXPECT_EQ(losses[1], loss(one, kBrief.x, kBrief.y));
    EXPECT_EQ(losses.back(), loss(last, kBrief.x, kBrief.y));
    EXPECT_EQ(train(kBrief.init, kBrief.x, kBrief.y, kBrief.lr, 0).size(), 1U);
}

TEST(BackpropTrain, LossDecreasesAtEveryStepAndApproachesZero) {
    const std::vector<double> losses =
        train(kBrief.init, kBrief.x, kBrief.y, kBrief.lr, kBrief.steps);
    for (std::size_t k = 1; k < losses.size(); ++k) {
        EXPECT_LT(losses[k], losses[k - 1]) << "step " << k;
    }
    EXPECT_LT(losses.back(), 1e-6 * losses.front());
}

TEST(BackpropTrain, IsDeterministic) {
    const auto a = train(kBrief.init, kBrief.x, kBrief.y, kBrief.lr, kBrief.steps);
    const auto b = train(kBrief.init, kBrief.x, kBrief.y, kBrief.lr, kBrief.steps);
    EXPECT_EQ(a, b);
}
