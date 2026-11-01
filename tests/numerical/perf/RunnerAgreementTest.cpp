// Serial vs parallel batch runners (spec §5.2, N5): bitwise agreement and index order.

#include "core/perf/BatchGenerator.hpp"
#include "core/perf/ParallelRunner.hpp"
#include "core/perf/SerialRunner.hpp"
#include "core/thermal/RodSolver.hpp"

#include <cstring>
#include <vector>

#include <gtest/gtest.h>

using uniforge::CancelToken;
using uniforge::ErrorCode;
using uniforge::perf::CaseResult;
using uniforge::perf::generateBatch;
using uniforge::perf::runParallel;
using uniforge::perf::runSerial;
using uniforge::thermal::RodProblem;

namespace {

std::vector<RodProblem> batch(int cases, int elements) {
    auto b = generateBatch(20261101, cases, elements);
    EXPECT_TRUE(b.ok());
    return b.value();
}

bool bitwiseEqual(const std::vector<CaseResult>& a, const std::vector<CaseResult>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].T.size() != b[i].T.size() ||
            std::memcmp(a[i].T.data(), b[i].T.data(), a[i].T.size() * sizeof(double)) != 0 ||
            std::memcmp(&a[i].Tmax, &b[i].Tmax, sizeof(double)) != 0 ||
            std::memcmp(&a[i].xAtTmax, &b[i].xAtTmax, sizeof(double)) != 0) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST(Runners, SerialMatchesDirectSolvePerCase) {
    const auto cases = batch(20, 40);
    const auto serial = runSerial(cases);
    ASSERT_TRUE(serial.ok());
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const auto direct = uniforge::thermal::solveRod(cases[i]);
        ASSERT_TRUE(direct.ok());
        EXPECT_EQ(serial.value()[i].T, direct.value().T) << "case " << i;
        EXPECT_EQ(serial.value()[i].T.front(), 100.0);
        EXPECT_EQ(serial.value()[i].T.back(), 20.0);
    }
}

TEST(Runners, ParallelIsBitwiseEqualToSerial) {
    const auto cases = batch(300, 200);
    const auto serial = runSerial(cases);
    ASSERT_TRUE(serial.ok());
    for (const int p : {1, 2, 3, 4, 8}) {
        const auto parallel = runParallel(cases, p);
        ASSERT_TRUE(parallel.ok()) << "p = " << p;
        EXPECT_TRUE(bitwiseEqual(serial.value(), parallel.value())) << "p = " << p;
    }
}

TEST(Runners, IndexOrderIsPreserved) {
    // Every case has a different k and q, so its peak temperature identifies it. A parallel
    // result written to the wrong slot would not match the case's own direct solve.
    const auto cases = batch(97, 30);
    const auto parallel = runParallel(cases, 4);
    ASSERT_TRUE(parallel.ok());
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const auto direct = uniforge::thermal::solveRod(cases[i]);
        ASSERT_TRUE(direct.ok());
        double tmax = direct.value().T[0];
        for (const double t : direct.value().T) {
            tmax = t > tmax ? t : tmax;
        }
        EXPECT_EQ(parallel.value()[i].Tmax, tmax) << "case " << i;
    }
}

TEST(Runners, PeakMatchesAnalyticalForBriefCase) {
    // Brief values (k = 10, q = 1e4): T_max = 188.2 °C at x = 0.42 m (nodal values are exact).
    RodProblem brief;
    brief.elements = 100;
    const auto r = runParallel({brief, brief}, 2);
    ASSERT_TRUE(r.ok());
    EXPECT_NEAR(r.value()[0].xAtTmax, 0.42, 1e-12);
    EXPECT_NEAR(r.value()[1].Tmax, 100.0 + 0.5 * 1e3 * 0.42 * 0.58 + (-80.0) * 0.42, 1e-9);
}

TEST(Runners, MoreWorkersThanCases) {
    const auto cases = batch(3, 25);
    const auto serial = runSerial(cases);
    const auto parallel = runParallel(cases, 16);
    ASSERT_TRUE(serial.ok() && parallel.ok());
    EXPECT_TRUE(bitwiseEqual(serial.value(), parallel.value()));
}

TEST(Runners, RejectsZeroWorkers) {
    const auto r = runParallel(batch(4, 4), 0);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::InvalidInput);
}

TEST(Runners, CancelReturnsErrorAndNoResults) {
    CancelToken cancel;
    cancel.requestCancel();
    const auto cases = batch(50, 50);
    const auto serial = runSerial(cases, &cancel);
    const auto parallel = runParallel(cases, 4, &cancel);
    ASSERT_FALSE(serial.ok());
    ASSERT_FALSE(parallel.ok());
    EXPECT_EQ(serial.error().code, ErrorCode::Cancelled);
    EXPECT_EQ(parallel.error().code, ErrorCode::Cancelled);
}

TEST(Runners, InvalidCasePropagatesItsError) {
    auto cases = batch(40, 10);
    cases[23].k = -1.0;
    const auto serial = runSerial(cases);
    const auto parallel = runParallel(cases, 4);
    ASSERT_FALSE(serial.ok());
    ASSERT_FALSE(parallel.ok());
    EXPECT_EQ(serial.error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(parallel.error().code, ErrorCode::InvalidInput);
}
