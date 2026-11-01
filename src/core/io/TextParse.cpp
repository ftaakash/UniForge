#include "core/io/TextParse.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <system_error>

namespace uniforge::io {

namespace {

// Drops one leading '+' unless another sign follows ("+-1" stays invalid).
std::string_view stripPlus(std::string_view text) noexcept {
    if (text.size() >= 2 && text[0] == '+' && text[1] != '+' && text[1] != '-') {
        text.remove_prefix(1);
    }
    return text;
}

} // namespace

NumberStatus parseDouble(std::string_view text, double& out) noexcept {
    text = stripPlus(text);
    if (text.empty()) {
        return NumberStatus::Invalid;
    }
    const char* last = text.data() + text.size();
    double value = 0.0;
    const auto [ptr, ec] = std::from_chars(text.data(), last, value, std::chars_format::general);
    if (ec == std::errc::invalid_argument || ptr != last) {
        return NumberStatus::Invalid;
    }
    if (ec == std::errc::result_out_of_range) {
        return NumberStatus::OutOfRange;
    }
    if (!std::isfinite(value)) {
        return NumberStatus::NonFinite;
    }
    out = value;
    return NumberStatus::Ok;
}

NumberStatus parseInt64(std::string_view text, std::int64_t& out) noexcept {
    text = stripPlus(text);
    if (text.empty()) {
        return NumberStatus::Invalid;
    }
    const char* last = text.data() + text.size();
    std::int64_t value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), last, value, 10);
    if (ec == std::errc::result_out_of_range) {
        return NumberStatus::OutOfRange;
    }
    if (ec != std::errc{} || ptr != last) {
        return NumberStatus::Invalid;
    }
    out = value;
    return NumberStatus::Ok;
}

std::string_view trimBlanks(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

std::string quoteForMessage(std::string_view text, std::size_t maxChars) {
    std::string out;
    const bool cut = text.size() > maxChars;
    if (cut) {
        text = text.substr(0, maxChars);
    }
    out.reserve(text.size() + 3);
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        out += (c >= 0x20 && c < 0x7F) ? ch : '?';
    }
    if (cut) {
        out += "...";
    }
    return out;
}

std::string formatCount(std::uintmax_t n) {
    std::string digits = std::to_string(n);
    std::string out;
    out.reserve(digits.size() + digits.size() / 3);
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) {
            out += ',';
        }
        out += digits[i];
    }
    return out;
}

std::string countOf(std::uintmax_t n, std::string_view one, std::string_view many) {
    return formatCount(n) + " " + std::string(n == 1 ? one : many);
}

std::string formatMegabytes(std::uintmax_t bytes) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.1f MB", static_cast<double>(bytes) / 1e6);
    return buffer;
}

} // namespace uniforge::io
