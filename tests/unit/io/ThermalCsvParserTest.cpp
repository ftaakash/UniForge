// Thermal CSV parser, positive cases: values, Tmin/Tmax, bbox, encodings, synthetic flag,
// duplicate detection in unsorted files, limits, progress and cancellation.

#include "core/io/ThermalCsvParser.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "core/common/CancelToken.hpp"

using uniforge::CancelToken;
using uniforge::ErrorCode;
using uniforge::Result;
using namespace uniforge::io;

namespace {

Result<ThermalNodeSet> parse(const std::string& text, const ImportLimits& limits = {}) {
    std::istringstream in(text);
    return parseThermalCsv(in, text.size(), limits);
}

std::string rows(std::size_t n) {
    std::string out(kThermalCsvHeader);
    out += '\n';
    for (std::size_t i = 1; i <= n; ++i) {
        out += std::to_string(i) + "," + std::to_string(i) + ",0,0,20\n";
    }
    return out;
}

} // namespace

TEST(ThermalCsvParser, ValuesRangeAndBbox) {
    const auto r = parse("node_id,x_m,y_m,z_m,temperature_C\n"
                         "10,0,0,0,100\n"
                         "11,0.5,-0.25,0.001,188.2\n"
                         "12,1,0.25,-0.001,-273.15\n");
    ASSERT_TRUE(r.ok()) << r.error().message;
    const ThermalNodeSet& s = r.value();
    ASSERT_EQ(s.nodes.size(), 3U);
    EXPECT_EQ(s.nodes[1].id, 11);
    EXPECT_EQ(s.nodes[1].p.y, -0.25);
    EXPECT_EQ(s.nodes[1].T_C, 188.2);
    EXPECT_EQ(s.Tmin, -273.15); // absolute zero itself is allowed
    EXPECT_EQ(s.Tmax, 188.2);
    EXPECT_EQ(s.bbMin.x, 0.0);
    EXPECT_EQ(s.bbMin.y, -0.25);
    EXPECT_EQ(s.bbMin.z, -0.001);
    EXPECT_EQ(s.bbMax.x, 1.0);
    EXPECT_EQ(s.bbMax.y, 0.25);
    EXPECT_EQ(s.bbMax.z, 0.001);
    EXPECT_FALSE(s.syntheticFlag); // only the file overload sets it
    EXPECT_EQ(s.stats.lines, 4U);
}

TEST(ThermalCsvParser, SingleRowAndNegativeIds) {
    const auto r = parse("node_id,x_m,y_m,z_m,temperature_C\n-5,1,2,3,4\n");
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().nodes[0].id, -5);
    EXPECT_EQ(r.value().Tmin, 4.0);
    EXPECT_EQ(r.value().Tmax, 4.0);
}

TEST(ThermalCsvParser, BomCrlfSpacesBlankLinesAndNoFinalNewlineFromFile) {
    const auto r = parseThermalCsvFile(std::filesystem::path(UNIFORGE_FIXTURES_DIR) / "csv" /
                                       "valid_bom_crlf.csv");
    ASSERT_TRUE(r.ok()) << r.error().message;
    const ThermalNodeSet& s = r.value();
    ASSERT_EQ(s.nodes.size(), 3U);
    EXPECT_EQ(s.nodes[0].T_C, 20.5);
    EXPECT_EQ(s.nodes[1].p.z, 1e-3);
    EXPECT_EQ(s.nodes[2].id, 3);
    EXPECT_EQ(s.Tmin, -273.15);
    EXPECT_EQ(s.Tmax, 100.0);
    EXPECT_EQ(s.stats.lines, 5U);
}

TEST(ThermalCsvParser, DuplicateFoundInUnsortedFile) {
    // Ids out of order; the earliest repeated row is reported, with the first occurrence.
    const auto r = parse("node_id,x_m,y_m,z_m,temperature_C\n"
                         "9,0,0,0,1\n"
                         "4,0,0,0,1\n"
                         "7,0,0,0,1\n"
                         "4,0,0,0,1\n"
                         "9,0,0,0,1\n"
                         "4,0,0,0,1\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(r.error().line, 5U);
    EXPECT_EQ(r.error().message, "Line 5: node_id 4 appears again (first on line 3).");
}

TEST(ThermalCsvParser, UnsortedUniqueIdsAreFine) {
    const auto r = parse("node_id,x_m,y_m,z_m,temperature_C\n3,0,0,0,1\n1,0,0,0,2\n2,0,0,0,3\n");
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().nodes.size(), 3U);
}

TEST(ThermalCsvParser, BelowAbsoluteZeroMessage) {
    const auto r = parse("node_id,x_m,y_m,z_m,temperature_C\n1,0,0,0,-273.16\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(r.error().message,
              "Line 2: temperature_C = -273.16 is below absolute zero (-273.15 \xC2\xB0"
              "C).");
}

TEST(ThermalCsvParser, SyntheticNameRule) {
    EXPECT_TRUE(isSyntheticName("rod_nodal_temperature_SYNTHETIC.csv"));
    EXPECT_TRUE(isSyntheticName("data/thermal/Synthetic-run.csv"));
    EXPECT_TRUE(isSyntheticName("synthetic.csv"));
    EXPECT_FALSE(isSyntheticName("ansys_export.csv"));
    EXPECT_FALSE(isSyntheticName("SYNTHETIC/real_export.csv")); // folder names do not count
}

TEST(ThermalCsvParser, FileOverloadSetsSyntheticFlag) {
    const auto dir = std::filesystem::temp_directory_path() / "uniforge_csv_flag_test";
    std::filesystem::create_directories(dir);
    for (const char* name : {"plate_SYNTHETIC.csv", "plate.csv"}) {
        {
            std::ofstream out(dir / name, std::ios::binary);
            out << rows(2);
        }
        const auto r = parseThermalCsvFile(dir / name);
        ASSERT_TRUE(r.ok()) << name;
        EXPECT_EQ(r.value().syntheticFlag, std::string(name) == "plate_SYNTHETIC.csv") << name;
    }
    std::filesystem::remove_all(dir);
}

TEST(ThermalCsvParser, NodeLimitExceeded) {
    ImportLimits limits;
    limits.maxNodes = 10;
    EXPECT_TRUE(parse(rows(10), limits).ok());
    const auto r = parse(rows(11), limits);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::LimitExceeded);
    EXPECT_EQ(r.error().line, 12U);
    EXPECT_NE(r.error().hint.find("10 nodes"), std::string::npos) << r.error().hint;
}

TEST(ThermalCsvParser, ByteLimitExceeded) {
    ImportLimits limits;
    limits.maxBytes = 100;
    const auto r = parse(rows(50), limits);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::LimitExceeded);
    EXPECT_NE(r.error().hint.find("0.0 MB"), std::string::npos); // 100 bytes shown in MB
}

TEST(ThermalCsvParser, ProgressAndCancellation) {
    const std::string text = rows(20000); // several 64 KB batches
    std::istringstream in(text);
    double last = 0.0;
    auto r = parseThermalCsv(in, text.size(), {}, [&](double f, std::string_view stage) {
        EXPECT_GE(f, last);
        EXPECT_EQ(stage, "Reading CSV");
        last = f;
    });
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().nodes.size(), 20000U);
    EXPECT_DOUBLE_EQ(last, 1.0);

    CancelToken cancel;
    std::istringstream in2(text);
    r = parseThermalCsv(
        in2, text.size(), {},
        [&](double f, std::string_view) {
            if (f > 0.5) {
                cancel.requestCancel();
            }
        },
        &cancel);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::Cancelled);
    EXPECT_EQ(r.error().hint, "The previously loaded data is kept.");
}
