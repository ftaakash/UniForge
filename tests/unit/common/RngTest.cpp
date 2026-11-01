// Portable Rng (spec §5.2, N8): fixed test vectors that must hold on MSVC, GCC and Clang.

#include "core/common/Rng.hpp"

#include <cstdint>
#include <random>

#include <gtest/gtest.h>

using uniforge::Rng;

// The C++ standard ([rand.predef]) fixes the 10000th output of a default-seeded mt19937_64.
TEST(Rng, EngineMatchesStandardReference) {
    std::mt19937_64 reference;
    reference.discard(9999);
    EXPECT_EQ(reference(), 9981545732273789042ULL);

    Rng rng(5489); // std::mt19937_64's default seed
    for (int i = 0; i < 9999; ++i) {
        (void)rng.next();
    }
    EXPECT_EQ(rng.next(), 9981545732273789042ULL);
}

TEST(Rng, RawOutputVectorsForBatchSeed) {
    Rng rng(20261101);
    EXPECT_EQ(rng.next(), 16427036408513627537ULL);
    EXPECT_EQ(rng.next(), 15463454668085141925ULL);
    EXPECT_EQ(rng.next(), 7725473311449279760ULL);
}

TEST(Rng, Uniform01UsesTop53Bits) {
    Rng a(42);
    Rng b(42);
    for (int i = 0; i < 1000; ++i) {
        const double expected = static_cast<double>(b.next() >> 11) * 0x1p-53;
        EXPECT_EQ(a.uniform01(), expected);
    }
}

TEST(Rng, UniformVectorsForBatchSeed) {
    // Exact values (17 significant digits) of the first draws the batch generator makes.
    Rng rng(20261101);
    EXPECT_EQ(rng.uniform(5.0, 50.0), 45.073014263620138);
    EXPECT_EQ(rng.uniform(1e3, 5e4), 42075.502305909111);
}

TEST(Rng, UniformStaysInHalfOpenInterval) {
    Rng rng(7);
    for (int i = 0; i < 100000; ++i) {
        const double x = rng.uniform(5.0, 50.0);
        ASSERT_GE(x, 5.0);
        ASSERT_LT(x, 50.0);
    }
}

TEST(Rng, SameSeedSameSequenceDifferentSeedDifferent) {
    Rng a(123);
    Rng b(123);
    Rng c(124);
    bool anyDifferent = false;
    for (int i = 0; i < 100; ++i) {
        const std::uint64_t va = a.next();
        EXPECT_EQ(va, b.next());
        anyDifferent = anyDifferent || va != c.next();
    }
    EXPECT_TRUE(anyDifferent);
}
