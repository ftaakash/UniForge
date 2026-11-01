#include "core/common/MetaJson.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "core/common/AtomicFile.hpp"
#include "core/common/CsvWriter.hpp"

namespace uniforge {

namespace {

std::string quote(std::string_view text) {
    std::string out = "\"";
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        switch (ch) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof buffer, "\\u%04x", static_cast<unsigned>(c));
                out += buffer;
            } else {
                out += ch; // UTF-8 bytes pass through unchanged
            }
        }
    }
    return out + "\"";
}

std::string number(double value) {
    return std::isfinite(value) ? formatFullPrecision(value) : std::string("null");
}

// Days since 1970-01-01 → civil date (H. Hinnant's algorithm, public domain).
void civilFromDays(std::int64_t z, std::int64_t& y, unsigned& m, unsigned& d) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<std::int64_t>(yoe) + era * 400 + (m <= 2 ? 1 : 0);
}

} // namespace

std::string formatUtcIso8601(std::chrono::system_clock::time_point t) {
    using namespace std::chrono;
    const std::int64_t secs = duration_cast<seconds>(t.time_since_epoch()).count();
    std::int64_t days = secs / 86400;
    std::int64_t rem = secs % 86400;
    if (rem < 0) {
        rem += 86400;
        --days;
    }
    std::int64_t year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civilFromDays(days, year, month, day);
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%04lld-%02u-%02uT%02d:%02d:%02dZ",
                  static_cast<long long>(year), month, day, static_cast<int>(rem / 3600),
                  static_cast<int>(rem % 3600 / 60), static_cast<int>(rem % 60));
    return buffer;
}

MetaJson& MetaJson::set(std::string_view key, std::string_view value) {
    entries_.push_back({std::string(key), quote(value), {}});
    return *this;
}

MetaJson& MetaJson::set(std::string_view key, double value) {
    entries_.push_back({std::string(key), number(value), {}});
    return *this;
}

MetaJson& MetaJson::set(std::string_view key, int value) {
    entries_.push_back({std::string(key), std::to_string(value), {}});
    return *this;
}

MetaJson& MetaJson::set(std::string_view key, bool value) {
    entries_.push_back({std::string(key), value ? "true" : "false", {}});
    return *this;
}

MetaJson& MetaJson::set(std::string_view key, const MetaJson& object) {
    entries_.push_back({std::string(key), {}, {object}});
    return *this;
}

MetaJson& MetaJson::set(std::string_view key, const std::vector<std::string>& values) {
    std::string array = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        array += (i == 0 ? "" : ", ") + quote(values[i]);
    }
    entries_.push_back({std::string(key), array + "]", {}});
    return *this;
}

MetaJson& MetaJson::set(std::string_view key, const std::vector<int>& values) {
    std::string array = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        array += (i == 0 ? "" : ", ") + std::to_string(values[i]);
    }
    entries_.push_back({std::string(key), array + "]", {}});
    return *this;
}

std::string MetaJson::render(int indent) const {
    if (entries_.empty()) {
        return "{}";
    }
    const std::string pad(static_cast<std::size_t>(indent + 2), ' ');
    std::string out = "{\n";
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_[i];
        out += pad + quote(e.key) + ": ";
        out += e.objects.empty() ? e.scalar : e.objects.front().render(indent + 2);
        out += i + 1 < entries_.size() ? ",\n" : "\n";
    }
    return out + std::string(static_cast<std::size_t>(indent), ' ') + "}";
}

Result<void> MetaJson::save(const std::filesystem::path& path) const {
    return writeFileAtomic(path, str());
}

} // namespace uniforge
