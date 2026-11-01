#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "core/common/Error.hpp"
#include "core/common/Result.hpp"
#include "core/io/ImportLimits.hpp"
#include "core/io/LineReader.hpp"
#include "core/io/TextParse.hpp"

// Error builders shared by the STL and CSV parsers, so both follow the spec §8.5 copy pattern:
// message = "<what happened> <where>", hint = "<how to fix>", line = 1-based where it applies.
namespace uniforge::io::detail {

// "Line 12: <what>", with error.line = 12.
[[nodiscard]] Error lineError(ErrorCode code, std::size_t line, std::string_view what,
                              std::string hint);

// Maps a LineReader failure (TooLong, TooLarge, Cancelled, ReadError) to an Error.
// `kind` names the file type in messages, e.g. "STL file".
[[nodiscard]] Error readerError(LineStatus status, const LineReader& reader,
                                const ImportLimits& limits, std::string_view kind);

// LimitExceeded for a file whose size is known up front to be over limits.maxBytes.
[[nodiscard]] Error fileTooLargeError(std::uintmax_t size, const ImportLimits& limits,
                                      std::string_view kind);

// Error for a number that failed to parse: ParseError for text, NonFinite for nan/inf or a value
// outside the double range. `field` names the value ("x_m", "vertex coordinate").
[[nodiscard]] Error numberError(NumberStatus status, std::size_t line, std::string_view field,
                                std::string_view token, std::string_view numberHint);

// Opens `path` for binary reading and returns its size. IoError names the file (not the full
// path, which may hold a user name) when it is missing, a folder or unreadable.
[[nodiscard]] Result<std::uintmax_t> openForImport(const std::filesystem::path& path,
                                                   std::ifstream& stream);

} // namespace uniforge::io::detail
