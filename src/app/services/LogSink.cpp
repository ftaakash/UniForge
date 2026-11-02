#include "app/services/LogSink.hpp"

#include <cstdio>
#include <ctime>

namespace uniforge {

std::string_view toString(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }
    return "INFO";
}

std::string formatTimestampUtc(std::chrono::system_clock::time_point t) {
    using namespace std::chrono;
    const auto sinceEpoch = t.time_since_epoch();
    auto secs = duration_cast<seconds>(sinceEpoch);
    auto millis = duration_cast<milliseconds>(sinceEpoch - secs).count();
    if (millis < 0) { // before 1970: borrow from the seconds so millis stays in [0, 1000)
        millis += 1000;
        secs -= seconds(1);
    }
    const auto tt = static_cast<std::time_t>(secs.count());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &tt);
#else
    gmtime_r(&tt, &utc);
#endif
    char buf[64]; // room for any int fields: GCC checks the worst case (-Wformat-truncation)
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", utc.tm_year + 1900,
                  utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec,
                  static_cast<int>(millis));
    return buf;
}

std::string formatLogLine(const LogEntry& entry) {
    std::string line = formatTimestampUtc(entry.time);
    line += ' ';
    line += toString(entry.level);
    line += ' ';
    line += entry.message;
    return line;
}

LogSink::LogSink(std::size_t capacity)
    : capacity_(capacity == 0 ? 1 : capacity) {}

bool LogSink::openFile(const std::filesystem::path& file) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream stream(file, std::ios::out | std::ios::app | std::ios::binary);
    if (!stream) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    file_ = std::move(stream);
    filePath_ = file;
    return true;
}

void LogSink::write(LogLevel level, std::string message) {
    LogEntry entry{std::chrono::system_clock::now(), level, std::move(message)};
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_ << formatLogLine(entry) << '\n';
        file_.flush(); // keep the file complete if the process dies
    }
    entries_.push_back(std::move(entry));
    while (entries_.size() > capacity_) {
        entries_.pop_front();
    }
    version_.fetch_add(1, std::memory_order_release);
}

std::vector<LogEntry> LogSink::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {entries_.begin(), entries_.end()};
}

std::filesystem::path LogSink::filePath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return filePath_;
}

} // namespace uniforge
