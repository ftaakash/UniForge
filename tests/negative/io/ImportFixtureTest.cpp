// Every malformed fixture in tests/fixtures/{stl,csv}/ is rejected with the exact ErrorCode and
// 1-based line (spec B3, §10: one file per failure mode). Messages follow the spec §8.5 pattern:
// "Line N: <what>" plus a non-empty hint on how to fix it.

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>

#include <gtest/gtest.h>

#include "core/common/Error.hpp"
#include "core/io/StlParser.hpp"
#include "core/io/ThermalCsvParser.hpp"

using uniforge::Error;
using uniforge::ErrorCode;
using uniforge::toString;

namespace {

struct Fixture {
    const char* file;
    ErrorCode code;
    std::optional<std::size_t> line;
};

std::ostream& operator<<(std::ostream& os, const Fixture& f) {
    return os << f.file;
}

std::string paramName(const testing::TestParamInfo<Fixture>& info) {
    return std::filesystem::path(info.param.file).stem().string();
}

std::filesystem::path fixture(const char* dir, const char* file) {
    return std::filesystem::path(UNIFORGE_FIXTURES_DIR) / dir / file;
}

void expectError(const Error& e, const Fixture& f) {
    EXPECT_EQ(toString(e.code), toString(f.code)) << e.message;
    EXPECT_EQ(e.line, f.line) << e.message;
    EXPECT_FALSE(e.message.empty());
    EXPECT_FALSE(e.hint.empty()) << e.message;
    if (f.line) {
        EXPECT_EQ(e.message.rfind("Line " + std::to_string(*f.line) + ": ", 0), 0U) << e.message;
    }
}

constexpr std::optional<std::size_t> kNoLine = std::nullopt;

class StlFixture : public testing::TestWithParam<Fixture> {};

TEST_P(StlFixture, RejectedWithCodeAndLine) {
    const Fixture& f = GetParam();
    const auto r = uniforge::io::parseAsciiStlFile(fixture("stl", f.file));
    ASSERT_FALSE(r.ok()) << f.file << " was accepted";
    expectError(r.error(), f);
}

INSTANTIATE_TEST_SUITE_P(
    Negative, StlFixture,
    testing::Values(Fixture{"empty.stl", ErrorCode::EmptyFile, kNoLine},
                    Fixture{"blank_lines_only.stl", ErrorCode::EmptyFile, kNoLine},
                    Fixture{"solid_without_facets.stl", ErrorCode::EmptyFile, 1},
                    Fixture{"binary.stl", ErrorCode::UnsupportedFormat, kNoLine},
                    Fixture{"binary_solid_header.stl", ErrorCode::UnsupportedFormat, kNoLine},
                    Fixture{"missing_solid.stl", ErrorCode::UnsupportedFormat, 1},
                    Fixture{"vertex_count_two.stl", ErrorCode::ParseError, 6},
                    Fixture{"vertex_count_four.stl", ErrorCode::ParseError, 7},
                    Fixture{"unknown_keyword.stl", ErrorCode::ParseError, 5},
                    Fixture{"vertex_missing_coordinate.stl", ErrorCode::ParseError, 4},
                    Fixture{"non_numeric_coordinate.stl", ErrorCode::ParseError, 5},
                    Fixture{"decimal_comma.stl", ErrorCode::ParseError, 4},
                    Fixture{"nan_coordinate.stl", ErrorCode::NonFinite, 6},
                    Fixture{"inf_coordinate.stl", ErrorCode::NonFinite, 4},
                    Fixture{"overflow_coordinate.stl", ErrorCode::NonFinite, 5},
                    Fixture{"nan_normal.stl", ErrorCode::NonFinite, 2},
                    Fixture{"truncated_in_facet.stl", ErrorCode::ParseError, 5},
                    Fixture{"missing_outer_loop.stl", ErrorCode::ParseError, 3},
                    Fixture{"missing_endloop.stl", ErrorCode::ParseError, 7},
                    Fixture{"text_after_endsolid.stl", ErrorCode::ParseError, 10},
                    Fixture{"overlong_line.stl", ErrorCode::ParseError, 2}),
    paramName);

class CsvFixture : public testing::TestWithParam<Fixture> {};

TEST_P(CsvFixture, RejectedWithCodeAndLine) {
    const Fixture& f = GetParam();
    const auto r = uniforge::io::parseThermalCsvFile(fixture("csv", f.file));
    ASSERT_FALSE(r.ok()) << f.file << " was accepted";
    expectError(r.error(), f);
}

INSTANTIATE_TEST_SUITE_P(
    Negative, CsvFixture,
    testing::Values(Fixture{"empty.csv", ErrorCode::EmptyFile, kNoLine},
                    Fixture{"header_only.csv", ErrorCode::EmptyFile, kNoLine},
                    Fixture{"wrong_header.csv", ErrorCode::ParseError, 1},
                    Fixture{"semicolon_separated.csv", ErrorCode::ParseError, 1},
                    Fixture{"missing_header.csv", ErrorCode::ParseError, 1},
                    Fixture{"missing_column.csv", ErrorCode::ParseError, 3},
                    Fixture{"extra_column.csv", ErrorCode::ParseError, 2},
                    Fixture{"non_numeric_value.csv", ErrorCode::ParseError, 4},
                    Fixture{"empty_value.csv", ErrorCode::ParseError, 3},
                    Fixture{"non_integer_node_id.csv", ErrorCode::ParseError, 2},
                    Fixture{"quoted_field.csv", ErrorCode::ParseError, 2},
                    Fixture{"nan_temperature.csv", ErrorCode::NonFinite, 3},
                    Fixture{"inf_coordinate.csv", ErrorCode::NonFinite, 2},
                    Fixture{"overflow_value.csv", ErrorCode::NonFinite, 3},
                    Fixture{"duplicate_node_id.csv", ErrorCode::InvalidInput, 5},
                    Fixture{"below_absolute_zero.csv", ErrorCode::InvalidInput, 3}),
    paramName);

// The messages that matter most to a user, checked word for word in the important parts.
TEST(ImportMessages, EmptyFileAndEmptySolidDiffer) {
    const auto empty = uniforge::io::parseAsciiStlFile(fixture("stl", "empty.stl"));
    const auto noFacets =
        uniforge::io::parseAsciiStlFile(fixture("stl", "solid_without_facets.stl"));
    ASSERT_FALSE(empty.ok());
    ASSERT_FALSE(noFacets.ok());
    EXPECT_EQ(empty.error().message, "The STL file is empty (0 bytes).");
    EXPECT_EQ(noFacets.error().message, "Line 1: the STL solid `empty` contains no facets.");
}

TEST(ImportMessages, BinaryStlSaysExportAsAscii) {
    const auto r = uniforge::io::parseAsciiStlFile(fixture("stl", "binary.stl"));
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("binary STL"), std::string::npos) << r.error().message;
    EXPECT_NE(r.error().hint.find("Export as ASCII STL"), std::string::npos) << r.error().hint;
}

TEST(ImportMessages, UnknownKeywordQuotesTheLine) {
    const auto r = uniforge::io::parseAsciiStlFile(fixture("stl", "unknown_keyword.stl"));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().message, "Line 5: expected `vertex x y z` but found `vertx 1 0 0`.");
    EXPECT_NE(r.error().hint.find("`vertx` is not an ASCII STL keyword"), std::string::npos);
}

TEST(ImportMessages, HeaderOnlyCsvSaysNoDataRows) {
    const auto r = uniforge::io::parseThermalCsvFile(fixture("csv", "header_only.csv"));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().message.rfind("No data rows", 0), 0U) << r.error().message;
}

TEST(ImportMessages, DuplicateIdNamesBothLines) {
    const auto r = uniforge::io::parseThermalCsvFile(fixture("csv", "duplicate_node_id.csv"));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().message, "Line 5: node_id 2 appears again (first on line 3).");
}

TEST(ImportMessages, SemicolonHeaderGetsSeparatorHint) {
    const auto r = uniforge::io::parseThermalCsvFile(fixture("csv", "semicolon_separated.csv"));
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().hint.find("';' as separator"), std::string::npos) << r.error().hint;
}

TEST(ImportMessages, MissingFileIsIoErrorWithoutFolder) {
    const auto r = uniforge::io::parseAsciiStlFile(fixture("stl", "does_not_exist.stl"));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::IoError);
    EXPECT_EQ(r.error().message, "Cannot open does_not_exist.stl: the file does not exist.");
}

TEST(ImportMessages, FolderIsIoError) {
    const auto r = uniforge::io::parseThermalCsvFile(fixture("csv", ""));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::IoError);
}

} // namespace
