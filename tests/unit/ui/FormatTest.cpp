#include "app/ui/Format.hpp"

#include <limits>

#include <gtest/gtest.h>

// Spec §8.3 examples, plus the edge cases each rule has to survive.
namespace uniforge::ui {
namespace {

TEST(Format, TemperatureHasFourDecimalsAndUnit) {
    EXPECT_EQ(formatTemperature(188.2), "188.2000 °C");
    EXPECT_EQ(formatTemperature(-273.15), "-273.1500 °C");
    EXPECT_EQ(formatTemperature(-0.00001), "0.0000 °C"); // no "-0.0000"
}

TEST(Format, FeErrorUsesFixedFromOneElseScientific) {
    EXPECT_EQ(formatFeError(1.25), "1.250000 °C");
    EXPECT_EQ(formatFeError(0.01953125), "1.953125e-02 °C");
    EXPECT_EQ(formatFeError(0.0), "0.000000e+00 °C");
    EXPECT_EQ(formatFeError(2.5e-15), "2.500000e-15 °C");
}

TEST(Format, RatioAndOrderFourDecimals) {
    EXPECT_EQ(formatRatio(4.0), "4.0000");
    EXPECT_EQ(formatRatio(1.99999999), "2.0000");
}

TEST(Format, DurationThreeSignificantAutoUnit) {
    EXPECT_EQ(formatDuration(0.412), "412 ms");
    EXPECT_EQ(formatDuration(1.234), "1.23 s");
    EXPECT_EQ(formatDuration(0.0124), "12.4 ms");
    EXPECT_EQ(formatDuration(18e-6), "18.0 µs");
    EXPECT_EQ(formatDuration(0.9996), "1.00 s"); // not "1000 ms"
    EXPECT_EQ(formatDuration(0.0), "0.00 ns");
}

TEST(Format, SpeedupAndEfficiency) {
    EXPECT_EQ(formatSpeedup(3.7123), "3.71×");
    EXPECT_EQ(formatEfficiency(0.928), "92.8 %");
}

TEST(Format, AgreementShowsToleranceAndVerdict) {
    EXPECT_EQ(formatAgreement(0.0, 1e-8), "0.00e+00 °C (≤ 1e-08 ✓)");
    EXPECT_EQ(formatAgreement(2e-8, 1e-8), "2.00e-08 °C (> 1e-08 ✕)");
}

TEST(Format, CoordinateSixSignificantKeepsZeros) {
    EXPECT_EQ(formatCoordinate(0.42), "0.420000 m");
    EXPECT_EQ(formatCoordinate(1.0), "1.00000 m");
    EXPECT_EQ(formatCoordinate(0.0), "0.00000 m");
}

TEST(Format, CountsAndBytes) {
    EXPECT_EQ(formatCount(0), "0");
    EXPECT_EQ(formatCount(999), "999");
    EXPECT_EQ(formatCount(1000), "1,000");
    EXPECT_EQ(formatCount(1234567), "1,234,567");
    EXPECT_EQ(formatBytes(512), "512 B");
    EXPECT_EQ(formatBytes(24800), "24.8 KB");
    EXPECT_EQ(formatBytes(1500000), "1.50 MB");
}

TEST(Format, ToleranceIsShortScientific) {
    EXPECT_EQ(formatTolerance(1e-9), "1e-09");
    EXPECT_EQ(formatTolerance(2.5e-3), "2.5e-03");
}

TEST(Format, NonFiniteIsADash) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_EQ(formatTemperature(nan), "—");
    EXPECT_EQ(formatFeError(inf), "—");
    EXPECT_EQ(formatRatio(nan), "—");
    EXPECT_EQ(formatDuration(-1.0), "—");
}

} // namespace
} // namespace uniforge::ui
