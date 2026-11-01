// LineReader: line ends, BOM, lines across buffer refills, limits, progress and cancellation.

#include "core/io/LineReader.hpp"

#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/common/CancelToken.hpp"
#include "core/io/ImportLimits.hpp"

using uniforge::CancelToken;
using uniforge::io::kLineReaderBufferBytes;
using uniforge::io::LineReader;
using uniforge::io::LineStatus;

namespace {

constexpr std::uintmax_t kNoByteLimit = ~std::uintmax_t{0};

std::vector<std::string> readAll(const std::string& text, LineStatus* last = nullptr,
                                 std::size_t maxLine = 4096) {
    std::istringstream in(text);
    LineReader reader(in, text.size(), kNoByteLimit, maxLine);
    std::vector<std::string> lines;
    std::string_view line;
    LineStatus s;
    while ((s = reader.next(line)) == LineStatus::Line) {
        lines.emplace_back(line);
        EXPECT_EQ(reader.lineNumber(), lines.size());
    }
    if (last != nullptr) {
        *last = s;
    }
    return lines;
}

} // namespace

TEST(LineReader, EmptyStreamHasNoLines) {
    LineStatus s = LineStatus::Line;
    EXPECT_TRUE(readAll("", &s).empty());
    EXPECT_EQ(s, LineStatus::End);
}

TEST(LineReader, LfCrlfAndMissingFinalLineEnd) {
    const std::vector<std::string> expected{"a", "bb", "", "ccc"};
    EXPECT_EQ(readAll("a\nbb\n\nccc"), expected);
    EXPECT_EQ(readAll("a\r\nbb\r\n\r\nccc\r\n"), expected);
    EXPECT_EQ(readAll("a\nbb\r\n\nccc\n"), expected);
}

TEST(LineReader, StripsUtf8BomOnlyAtStart) {
    EXPECT_EQ(readAll("\xEF\xBB\xBFsolid x\n"), std::vector<std::string>{"solid x"});
    EXPECT_EQ(readAll("\xEF\xBB\xBF"), std::vector<std::string>{});
    EXPECT_EQ(readAll("a\n\xEF\xBB\xBF"
                      "b\n"),
              (std::vector<std::string>{"a", "\xEF\xBB\xBF"
                                             "b"}));
}

TEST(LineReader, LinesSpanningBufferRefills) {
    // 40-char lines do not divide 64 KB evenly, so many lines straddle a refill boundary.
    std::string text;
    std::vector<std::string> expected;
    for (int i = 0; i < 10000; ++i) {
        std::string line = std::to_string(i);
        line.resize(40, 'x');
        expected.push_back(line);
        text += line + (i % 2 == 0 ? "\r\n" : "\n");
    }
    EXPECT_GT(text.size(), 4 * kLineReaderBufferBytes);
    EXPECT_EQ(readAll(text), expected);
}

TEST(LineReader, CrlfSplitAcrossRefill) {
    // Put the '\r' as the last byte of the first 64 KB block and the '\n' as the first of the next.
    std::string first(kLineReaderBufferBytes - 1, 'a');
    first[100] = '\n';
    const std::string text = first + "\r\nlast";
    const auto lines = readAll(text, nullptr, kLineReaderBufferBytes);
    ASSERT_EQ(lines.size(), 3U);
    EXPECT_EQ(lines[1].size(), kLineReaderBufferBytes - 1 - 101);
    EXPECT_EQ(lines[2], "last");
}

TEST(LineReader, TooLongLineReportsItsNumber) {
    const std::string text = "short\n" + std::string(100, 'x') + "\nnext\n";
    std::istringstream in(text);
    LineReader reader(in, text.size(), kNoByteLimit, 99);
    std::string_view line;
    EXPECT_EQ(reader.next(line), LineStatus::Line);
    EXPECT_EQ(reader.next(line), LineStatus::TooLong);
    EXPECT_EQ(reader.lineNumber(), 2U);
}

TEST(LineReader, LineWithoutLineEndLongerThanBuffer) {
    const std::string text(3 * kLineReaderBufferBytes, 'x');
    std::istringstream in(text);
    LineReader reader(in, text.size(), kNoByteLimit, 1'000'000); // clamped to the buffer
    std::string_view line;
    EXPECT_EQ(reader.next(line), LineStatus::TooLong);
    EXPECT_EQ(reader.lineNumber(), 1U);
    EXPECT_LE(reader.bytesRead(), 2 * kLineReaderBufferBytes); // stopped early, memory bounded
}

TEST(LineReader, ByteLimitIsEnforcedEvenAfterPeek) {
    const std::string text = "a\nb\nc\n";
    std::istringstream in(text);
    LineReader reader(in, 0, 3, 4096);
    EXPECT_EQ(reader.peek(84), text); // peek reads the whole small stream
    std::string_view line;
    EXPECT_EQ(reader.next(line), LineStatus::TooLarge);
}

TEST(LineReader, ProgressIsMonotonicAndReachesOne) {
    const std::string text(5 * kLineReaderBufferBytes + 123, '\n');
    std::istringstream in(text);
    std::vector<double> fractions;
    LineReader reader(in, text.size(), kNoByteLimit, 4096, [&](double f, std::string_view stage) {
        fractions.push_back(f);
        EXPECT_EQ(stage, "Reading");
    });
    std::string_view line;
    while (reader.next(line) == LineStatus::Line) {
    }
    ASSERT_GE(fractions.size(), 6U);
    for (std::size_t i = 1; i < fractions.size(); ++i) {
        EXPECT_GE(fractions[i], fractions[i - 1]);
    }
    EXPECT_DOUBLE_EQ(fractions.back(), 1.0);
}

TEST(LineReader, CancelStopsAtNextBatch) {
    const std::string text(10 * kLineReaderBufferBytes, '\n');
    std::istringstream in(text);
    CancelToken cancel;
    LineReader reader(
        in, text.size(), kNoByteLimit, 4096,
        [&](double, std::string_view) { cancel.requestCancel(); }, &cancel);
    std::string_view line;
    LineStatus s;
    std::size_t lines = 0;
    while ((s = reader.next(line)) == LineStatus::Line) {
        ++lines;
    }
    EXPECT_EQ(s, LineStatus::Cancelled);
    EXPECT_EQ(lines, kLineReaderBufferBytes); // exactly the first batch was delivered
    EXPECT_EQ(reader.bytesRead(), kLineReaderBufferBytes);
}
