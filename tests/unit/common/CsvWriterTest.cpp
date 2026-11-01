// CsvWriter: full-precision numbers, ASCII-only content, row checks and atomic save.

#include "core/common/CsvWriter.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "core/common/AtomicFile.hpp"

namespace fs = std::filesystem;
using uniforge::CsvNumberFormat;
using uniforge::CsvWriter;
using uniforge::ErrorCode;
using uniforge::formatFullPrecision;

namespace {

fs::path tempDir(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("uniforge_test_" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

std::string readBinary(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

TEST(CsvWriter, FullPrecisionRoundTrips) {
    for (const double v : {0.1, 1.0 / 3.0, 188.2, -1e-300, 6.02214076e23, 0.0}) {
        const std::string s = formatFullPrecision(v);
        EXPECT_EQ(std::strtod(s.c_str(), nullptr), v) << s;
    }
    EXPECT_EQ(formatFullPrecision(0.1), "0.10000000000000001");
    EXPECT_EQ(formatFullPrecision(1.25), "1.25");
    EXPECT_EQ(formatFullPrecision(20.0), "20");
}

TEST(CsvWriter, WritesHeaderRowsAndLfLineEnds) {
    CsvWriter csv({"N", "h_m", "note"});
    csv.beginRow();
    csv.add(10);
    csv.add(0.1);
    csv.add("first");
    csv.beginRow();
    csv.add(20);
    csv.add(0.05);
    csv.addBlank();
    ASSERT_TRUE(csv.validate().ok());
    EXPECT_EQ(csv.rowCount(), 2U);
    EXPECT_EQ(csv.str(), "N,h_m,note\n10,0.10000000000000001,first\n20,0.050000000000000003,\n");
}

TEST(CsvWriter, RejectsNonFiniteNumbers) {
    CsvWriter csv({"x"});
    csv.beginRow();
    csv.add(std::numeric_limits<double>::quiet_NaN());
    const auto r = csv.validate();
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::NonFinite);
}

TEST(CsvWriter, RejectsWrongCellCount) {
    CsvWriter csv({"a", "b"});
    csv.beginRow();
    csv.add(1);
    csv.beginRow(); // first row had one cell
    csv.add(1);
    csv.add(2);
    EXPECT_FALSE(csv.validate().ok());

    CsvWriter open({"a", "b"});
    open.beginRow();
    open.add(1);
    EXPECT_FALSE(open.validate().ok()) << "an unfinished last row is checked too";
}

TEST(CsvWriter, RejectsNonAsciiAndSeparatorsInText) {
    CsvWriter degree({"T_\xC2\xB0" "C"}); // UTF-8 degree sign in a column name
    EXPECT_FALSE(degree.validate().ok());

    CsvWriter comma({"a"});
    comma.beginRow();
    comma.add("x,y");
    EXPECT_FALSE(comma.validate().ok());
}

TEST(CsvWriter, SaveIsAtomicAndByteExact) {
    const fs::path dir = tempDir("csv_save");
    const fs::path file = dir / "sub" / "out.csv";
    CsvWriter csv({"x_m"});
    csv.beginRow();
    csv.add(0.5);
    ASSERT_TRUE(csv.save(file).ok());
    EXPECT_EQ(readBinary(file), "x_m\n0.5\n");
    EXPECT_FALSE(fs::exists(dir / "sub" / "out.csv.tmp")) << "temp file must be renamed away";

    // A second save replaces the file.
    CsvWriter again({"x_m"});
    again.beginRow();
    again.add(2.0);
    ASSERT_TRUE(again.save(file).ok());
    EXPECT_EQ(readBinary(file), "x_m\n2\n");
    fs::remove_all(dir);
}

TEST(CsvWriter, InvalidCsvLeavesExistingFileUntouched) {
    const fs::path dir = tempDir("csv_keep");
    const fs::path file = dir / "out.csv";
    ASSERT_TRUE(uniforge::writeFileAtomic(file, "old\n").ok());

    CsvWriter bad({"x"});
    bad.beginRow();
    bad.add(std::numeric_limits<double>::infinity());
    EXPECT_FALSE(bad.save(file).ok());
    EXPECT_EQ(readBinary(file), "old\n");
    fs::remove_all(dir);
}

TEST(AtomicFile, ReportsIoErrorWhenFolderCannotBeCreated) {
    const fs::path dir = tempDir("atomic_fail");
    const fs::path blocker = dir / "file";
    ASSERT_TRUE(uniforge::writeFileAtomic(blocker, "x").ok());
    // A regular file stands where the parent folder should be.
    const auto r = uniforge::writeFileAtomic(blocker / "child.csv", "data");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::IoError);
    EXPECT_FALSE(r.error().hint.empty());
    fs::remove_all(dir);
}

TEST(CsvWriter, ShortestFormatForSampleData) {
    CsvWriter csv({"x_m", "T_C"}, CsvNumberFormat::Shortest);
    csv.beginRow();
    csv.add(0.42);
    csv.add(-0.0);
    csv.beginRow();
    csv.add(1e-05);
    csv.add(188.2);
    EXPECT_EQ(csv.str(), "x_m,T_C\n0.42,0\n1e-05,188.2\n");
}
