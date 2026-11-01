// Invalid thermal inputs give typed errors, never UB or NaN results (spec §5.2), and a
// cancelled study returns Cancelled.

#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "core/common/CancelToken.hpp"
#include "core/thermal/ConvergenceStudy.hpp"
#include "core/thermal/ErrorAnalysis.hpp"
#include "core/thermal/RodProblem.hpp"
#include "core/thermal/RodSolver.hpp"

using uniforge::CancelToken;
using uniforge::ErrorCode;
using namespace uniforge::thermal;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

ErrorCode solveError(const RodProblem& p) {
    const auto r = solveRod(p);
    if (r.ok()) {
        ADD_FAILURE() << "expected the solve to fail";
        return ErrorCode::Internal;
    }
    EXPECT_FALSE(r.error().message.empty());
    EXPECT_FALSE(r.error().hint.empty());
    return r.error().code;
}

} // namespace

TEST(ThermalInput, DefaultProblemIsValid) { EXPECT_TRUE(RodProblem{}.validate().ok()); }

TEST(ThermalInput, TooFewElements) {
    for (const int n : {0, -1, std::numeric_limits<int>::min()}) {
        RodProblem p;
        p.elements = n;
        EXPECT_EQ(solveError(p), ErrorCode::InvalidInput) << "N = " << n;
    }
}

TEST(ThermalInput, TooManyElements) {
    RodProblem p;
    p.elements = RodProblem::kMaxElements + 1;
    EXPECT_EQ(solveError(p), ErrorCode::LimitExceeded);
}

TEST(ThermalInput, NonPositiveLengthAreaConductivity) {
    for (const double v : {0.0, -1.0}) {
        RodProblem p;
        p.L = v;
        EXPECT_EQ(solveError(p), ErrorCode::InvalidInput) << "L = " << v;
        p = RodProblem{};
        p.A = v;
        EXPECT_EQ(solveError(p), ErrorCode::InvalidInput) << "A = " << v;
        p = RodProblem{};
        p.k = v;
        EXPECT_EQ(solveError(p), ErrorCode::InvalidInput) << "k = " << v;
    }
}

TEST(ThermalInput, NonFiniteParameters) {
    for (const double bad : {kNaN, kInf, -kInf}) {
        for (double RodProblem::*field : {&RodProblem::L, &RodProblem::A, &RodProblem::k,
                                          &RodProblem::q, &RodProblem::T0, &RodProblem::TL}) {
            RodProblem p;
            p.*field = bad;
            EXPECT_EQ(solveError(p), ErrorCode::NonFinite);
        }
    }
}

TEST(ThermalInput, MessageNamesParameterAndUnit) {
    RodProblem p;
    p.k = -3.0;
    const auto r = p.validate();
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("Conductivity k"), std::string::npos);
    EXPECT_NE(r.error().message.find("W/(m·K)"), std::string::npos);
}

TEST(ThermalInput, StudyRejectsBadMeshLists) {
    EXPECT_EQ(convergenceStudy(RodProblem{}, {}).error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(convergenceStudy(RodProblem{}, {10, 10}).error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(convergenceStudy(RodProblem{}, {20, 10}).error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(convergenceStudy(RodProblem{}, {0, 10}).error().code, ErrorCode::InvalidInput);
    RodProblem bad;
    bad.q = kNaN;
    EXPECT_EQ(convergenceStudy(bad, {10, 20}).error().code, ErrorCode::NonFinite);
}

TEST(ThermalInput, AnalyseRejectsMismatchedSolution) {
    const auto sol = solveRod(RodProblem{});
    ASSERT_TRUE(sol.ok());
    RodProblem other;
    other.elements = 20;
    EXPECT_EQ(analyse(other, sol.value()).error().code, ErrorCode::InvalidInput);
}

TEST(ThermalCancel, CancelledSolveReturnsCancelled) {
    CancelToken token;
    token.requestCancel();
    EXPECT_EQ(solveRod(RodProblem{}, &token).error().code, ErrorCode::Cancelled);
}

TEST(ThermalCancel, StudyCancelledMidwayReturnsCancelledAndNoRows) {
    CancelToken token;
    int calls = 0;
    const auto rows = convergenceStudy(
        RodProblem{}, {10, 20, 40, 80},
        [&](double, std::string_view) {
            if (++calls == 2) {
                token.requestCancel(); // as if the user pressed Cancel during mesh 2
            }
        },
        &token);
    ASSERT_FALSE(rows.ok());
    EXPECT_EQ(rows.error().code, ErrorCode::Cancelled);
    EXPECT_EQ(calls, 2) << "the study must stop before solving mesh 3";
}
