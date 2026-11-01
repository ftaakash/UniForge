#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace uniforge {

enum class LogLevel { Debug, Info, Warn, Error };

struct LogEntry {
    std::chrono::system_clock::time_point time;
    LogLevel level = LogLevel::Info;
    std::string message;
};

[[nodiscard]] std::string_view toString(LogLevel level) noexcept;

// "2026-11-03T10:22:04.512Z" (UTC, millisecond precision).
[[nodiscard]] std::string formatTimestampUtc(std::chrono::system_clock::time_point t);

// "2026-11-03T10:22:04.512Z INFO job=7 demo.steps done 1.02 s"
[[nodiscard]] std::string formatLogLine(const LogEntry& entry);

// Thread-safe application log. Keeps the most recent `capacity` entries in memory for the
// LogPanel and appends every entry to a file once openFile() has succeeded.
class LogSink {
public:
    explicit LogSink(std::size_t capacity = 5000);

    // Opens (appends to) the log file, creating parent directories. Returns false on failure;
    // logging then continues in memory only.
    bool openFile(const std::filesystem::path& file);

    void write(LogLevel level, std::string message);
    void debug(std::string message) { write(LogLevel::Debug, std::move(message)); }
    void info(std::string message) { write(LogLevel::Info, std::move(message)); }
    void warn(std::string message) { write(LogLevel::Warn, std::move(message)); }
    void error(std::string message) { write(LogLevel::Error, std::move(message)); }

    [[nodiscard]] std::vector<LogEntry> snapshot() const;
    // Incremented on every write; lets the UI skip copying when nothing changed.
    [[nodiscard]] std::uint64_t version() const noexcept {
        return version_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::filesystem::path filePath() const;

private:
    mutable std::mutex mutex_;
    std::deque<LogEntry> entries_;
    std::size_t capacity_;
    std::ofstream file_;
    std::filesystem::path filePath_;
    std::atomic<std::uint64_t> version_{0};
};

} // namespace uniforge
