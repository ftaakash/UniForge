// Task D batch generator (spec §5.2, §7.4): fixed seed → identical batch on every compiler.

#include "core/perf/BatchGenerator.hpp"

#include <gtest/gtest.h>

using uniforge::ErrorCode;
using uniforge::perf::generateBatch;
using uniforge::thermal::RodProblem;

TEST(BatchGenerator, SameSeedGivesIdenticalBatch) {
    const auto a = generateBatch(20261101, 200, 50);
    const auto b = generateBatch(20261101, 200, 50);
    ASSERT_TRUE(a.ok());
    ASSERT_TRUE(b.ok());
    ASSERT_EQ(a.value().size(), 200U);
    for (std::size_t i = 0; i < a.value().size(); ++i) {
        EXPECT_EQ(a.value()[i].k, b.value()[i].k) << "case " << i;
        EXPECT_EQ(a.value()[i].q, b.value()[i].q) << "case " << i;
    }
}

TEST(BatchGenerator, DifferentSeedGivesDifferentBatch) {
    const auto a = generateBatch(1, 10, 10);
    const auto b = generateBatch(2, 10, 10);
    ASSERT_TRUE(a.ok() && b.ok());
    EXPECT_NE(a.value()[0].k, b.value()[0].k);
}

TEST(BatchGenerator, PortableValuesForReportSeed) {
    // Pinned on MSVC; the Linux CI jobs check the same numbers with GCC.
    const auto batch = generateBatch(20261101, 3, 10);
    ASSERT_TRUE(batch.ok());
    EXPECT_EQ(batch.value()[0].k, 45.073014263620138);
    EXPECT_EQ(batch.value()[0].q, 42075.502305909111);
    EXPECT_EQ(batch.value()[2].k, 24.001227305521255);
    EXPECT_EQ(batch.value()[2].q, 37439.788599435145);
}

TEST(BatchGenerator, ParametersInRangeAndFixedValuesFromBrief) {
    const auto batch = generateBatch(99, 5000, 7);
    ASSERT_TRUE(batch.ok());
    const RodProblem brief;
    for (const RodProblem& p : batch.value()) {
        ASSERT_GE(p.k, 5.0);
        ASSERT_LT(p.k, 50.0);
        ASSERT_GE(p.q, 1e3);
        ASSERT_LT(p.q, 5e4);
        ASSERT_EQ(p.elements, 7);
        ASSERT_EQ(p.L, brief.L);
        ASSERT_EQ(p.A, brief.A);
        ASSERT_EQ(p.T0, brief.T0);
        ASSERT_EQ(p.TL, brief.TL);
        ASSERT_TRUE(p.validate().ok());
    }
}

TEST(BatchGenerator, PrefixIsStableWhenCaseCountGrows) {
    const auto small = generateBatch(5, 10, 20);
    const auto large = generateBatch(5, 1000, 20);
    ASSERT_TRUE(small.ok() && large.ok());
    for (std::size_t i = 0; i < 10; ++i) {
        EXPECT_EQ(small.value()[i].k, large.value()[i].k);
        EXPECT_EQ(small.value()[i].q, large.value()[i].q);
    }
}

TEST(BatchGenerator, RejectsInvalidSizes) {
    EXPECT_EQ(generateBatch(1, 0, 10).error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(generateBatch(1, 10, 0).error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(generateBatch(1, -5, 10).error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(generateBatch(1, 10, RodProblem::kMaxElements + 1).error().code,
              ErrorCode::InvalidInput);
}
