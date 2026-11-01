// Locale-independent number parsing and message helpers.

#include "core/io/TextParse.hpp"

#include <clocale>
#include <cstdint>
#include <limits>
#include <string>

#include <gtest/gtest.h>

#include "core/common/CsvWriter.hpp"

using namespace uniforge::io;
using uniforge::formatShortest;

TEST(TextParse, Doubles) {
    double v = 0.0;
    EXPECT_EQ(parseDouble("0.5", v), NumberStatus::Ok);
    EXPECT_EQ(v, 0.5);
    EXPECT_EQ(parseDouble("-1.25e-3", v), NumberStatus::Ok);
    EXPECT_EQ(v, -1.25e-3);
    EXPECT_EQ(parseDouble("+1.000000e+00", v), NumberStatus::Ok);
    EXPECT_EQ(v, 1.0);
    EXPECT_EQ(parseDouble("1E2", v), NumberStatus::Ok);
    EXPECT_EQ(v, 100.0);
    EXPECT_EQ(parseDouble("7", v), NumberStatus::Ok);
    EXPECT_EQ(v, 7.0);
}

TEST(TextParse, RejectsMalformedDoubles) {
    double v = 42.0;
    for (const char* bad :
         {"", "+", "abc", "1.5x", "0,5", " 1", "1 ", "++1", "+-1", "0x10", "1e", "--1"}) {
        EXPECT_EQ(parseDouble(bad, v), NumberStatus::Invalid) << "'" << bad << "'";
    }
    EXPECT_EQ(v, 42.0); // untouched on failure
}

TEST(TextParse, RejectsNonFiniteAndOutOfRange) {
    double v = 0.0;
    for (const char* bad : {"nan", "NaN", "-nan", "inf", "-inf", "Infinity", "+inf"}) {
        EXPECT_EQ(parseDouble(bad, v), NumberStatus::NonFinite) << bad;
    }
    EXPECT_EQ(parseDouble("1e400", v), NumberStatus::OutOfRange);
    EXPECT_EQ(parseDouble("-1e400", v), NumberStatus::OutOfRange);
    EXPECT_EQ(parseDouble("1e-400", v), NumberStatus::OutOfRange); // underflow is rejected too
    EXPECT_EQ(parseDouble("1.7976931348623157e308", v), NumberStatus::Ok);
}

TEST(TextParse, IgnoresTheCLocaleDecimalSeparator) {
    // A German locale would make strtod expect ','. from_chars must not care. The locale may be
    // missing on the machine; the test is meaningful only where setlocale succeeds.
    const std::string saved = std::setlocale(LC_NUMERIC, nullptr);
    const bool switched = std::setlocale(LC_NUMERIC, "de-DE") != nullptr ||
                          std::setlocale(LC_NUMERIC, "de_DE.UTF-8") != nullptr;
    double v = 0.0;
    EXPECT_EQ(parseDouble("0.25", v), NumberStatus::Ok);
    EXPECT_EQ(v, 0.25);
    EXPECT_EQ(parseDouble("0,25", v), NumberStatus::Invalid);
    std::setlocale(LC_NUMERIC, saved.c_str());
    if (!switched) {
        GTEST_SKIP() << "no German locale installed; checked in the C locale only";
    }
}

TEST(TextParse, Int64) {
    std::int64_t v = 0;
    EXPECT_EQ(parseInt64("17", v), NumberStatus::Ok);
    EXPECT_EQ(v, 17);
    EXPECT_EQ(parseInt64("+3", v), NumberStatus::Ok);
    EXPECT_EQ(v, 3);
    EXPECT_EQ(parseInt64("-9223372036854775808", v), NumberStatus::Ok);
    EXPECT_EQ(v, std::numeric_limits<std::int64_t>::min());
    EXPECT_EQ(parseInt64("9223372036854775808", v), NumberStatus::OutOfRange);
    for (const char* bad : {"", "1.5", "1e3", "abc", "12a", " 1"}) {
        EXPECT_EQ(parseInt64(bad, v), NumberStatus::Invalid) << bad;
    }
}

TEST(TextParse, Helpers) {
    EXPECT_EQ(trimBlanks(" \t a b \t"), "a b");
    EXPECT_EQ(trimBlanks("   "), "");
    EXPECT_EQ(quoteForMessage("ok\x01\xC3\xA9"), "ok???");
    EXPECT_EQ(quoteForMessage("abcdef", 3), "abc...");
    EXPECT_EQ(formatCount(0), "0");
    EXPECT_EQ(formatCount(999), "999");
    EXPECT_EQ(formatCount(5'000'000), "5,000,000");
    EXPECT_EQ(formatMegabytes(536'870'912), "536.9 MB");
}

TEST(TextParse, ShortestFormatRoundTrips) {
    EXPECT_EQ(formatShortest(0.005), "0.005");
    EXPECT_EQ(formatShortest(-0.0), "0");
    EXPECT_EQ(formatShortest(1.0), "1");
    EXPECT_EQ(formatShortest(1e-05), "1e-05");
    for (const double v :
         {0.1, 1.0 / 3.0, 188.2, -273.15, 6.02214076e23, 2.2250738585072014e-308}) {
        double back = 0.0;
        ASSERT_EQ(parseDouble(formatShortest(v), back), NumberStatus::Ok) << v;
        EXPECT_EQ(back, v);
    }
}
