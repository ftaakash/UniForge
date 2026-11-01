#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace uniforge::io {

enum class NumberStatus {
    Ok,
    Invalid,   // not a number, or trailing characters ("1.5x", "abc", "")
    NonFinite, // nan or inf in any spelling std::from_chars accepts
    OutOfRange // overflow or underflow of the target type
};

// Locale-independent number parsing with std::from_chars (spec §7.2): '.' is always the decimal
// separator whatever the system region, and the whole token must be consumed. A single leading
// '+' is accepted because some exporters write "+1.000000e+00".
[[nodiscard]] NumberStatus parseDouble(std::string_view text, double& out) noexcept;
[[nodiscard]] NumberStatus parseInt64(std::string_view text, std::int64_t& out) noexcept;

// Strips spaces and tabs at both ends.
[[nodiscard]] std::string_view trimBlanks(std::string_view text) noexcept;

// Text from a user file made safe for an error message: control and non-ASCII bytes become '?',
// and anything longer than maxChars is cut with "...".
[[nodiscard]] std::string quoteForMessage(std::string_view text, std::size_t maxChars = 60);

// 5000000 -> "5,000,000".
[[nodiscard]] std::string formatCount(std::uintmax_t n);

// Count with a noun: (1, "facet", "facets") -> "1 facet", (2000, ...) -> "2,000 facets".
[[nodiscard]] std::string countOf(std::uintmax_t n, std::string_view one, std::string_view many);

// Bytes as MB (10^6 bytes) with one decimal: 536870912 -> "536.9 MB".
[[nodiscard]] std::string formatMegabytes(std::uintmax_t bytes);

} // namespace uniforge::io
