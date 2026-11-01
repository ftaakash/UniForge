#include "app/services/LogSink.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

using uniforge::LogEntry;
using uniforge::LogLevel;
using uniforge::LogSink;

namespace {

std::chrono::system_clock::time_point utc(long long epochSeconds, int millis) {
    return std::chrono::system_clock::time_point(std::chrono::seconds(epochSeconds) +
                                                 std::chrono::milliseconds(millis));
}

} // namespace

TEST(LogSink, FormatsUtcTimestampWithMilliseconds) {
    // 1793701324 s = 2026-11-03T10:22:04Z (checked with `date -u -d @1793701324`).
    EXPECT_EQ(uniforge::formatTimestampUtc(utc(1793701324, 512)), "2026-11-03T10:22:04.512Z");
    EXPECT_EQ(uniforge::formatTimestampUtc(utc(0, 0)), "1970-01-01T00:00:00.000Z");
}

TEST(LogSink, FormatsLineAsTimestampLevelMessage) {
    const LogEntry entry{utc(1793701324, 7), LogLevel::Warn, "job=3 demo.steps rejected"};
    EXPECT_EQ(uniforge::formatLogLine(entry),
              "2026-11-03T10:22:04.007Z WARN job=3 demo.steps rejected");
}

TEST(LogSink, KeepsOnlyTheMostRecentEntries) {
    LogSink log(3);
    for (int i = 0; i < 5; ++i) {
        log.info("entry " + std::to_string(i));
    }
    const auto entries = log.snapshot();
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries.front().message, "entry 2");
    EXPECT_EQ(entries.back().message, "entry 4");
    EXPECT_EQ(log.version(), 5u);
}

TEST(LogSink, AppendsEveryEntryToTheFile) {
    const auto dir = std::filesystem::temp_directory_path() / "uniforge_logsink_test";
    std::filesystem::remove_all(dir);
    const auto file = dir / "logs" / "uniforge.log"; // parent directories are created
    {
        LogSink log(1); // the file keeps everything even when memory keeps one entry
        ASSERT_TRUE(log.openFile(file));
        log.info("first");
        log.error("second");
        EXPECT_EQ(log.filePath(), file);
    }
    std::ifstream in(file);
    std::string line1;
    std::string line2;
    std::getline(in, line1);
    std::getline(in, line2);
    EXPECT_NE(line1.find(" INFO first"), std::string::npos) << line1;
    EXPECT_NE(line2.find(" ERROR second"), std::string::npos) << line2;
    in.close();
    std::filesystem::remove_all(dir);
}
