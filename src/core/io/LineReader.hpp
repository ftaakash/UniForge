#pragma once

#include <cstddef>
#include <cstdint>
#include <istream>
#include <string>
#include <string_view>
#include <vector>

#include "core/common/CancelToken.hpp"
#include "core/common/Progress.hpp"

namespace uniforge::io {

enum class LineStatus {
    Line,      // `line` holds the next line
    End,       // no more lines
    TooLong,   // the line being read is longer than maxLineBytes (lineNumber() is that line)
    TooLarge,  // more than maxBytes were read
    Cancelled, // the CancelToken was set
    ReadError  // the stream reported a read failure (badbit)
};

// Streams text lines from an std::istream through a fixed 64 KB buffer, so memory stays
// constant whatever the file size (spec §7.2).
//   - LF and CRLF line ends; a final line without a line end is still returned.
//   - A UTF-8 byte-order mark at the very start is skipped.
//   - Every buffer refill (one "batch" of up to 64 KB) reports progress = bytes read / sizeHint
//     and checks the CancelToken.
//   - Returned lines are views into the buffer, valid until the next call to next().
//
//     LineReader reader(in, size, limits.maxBytes, limits.maxLineBytes, progress, cancel, "STL");
//     std::string_view line;
//     while (reader.next(line) == LineStatus::Line) { ... reader.lineNumber() ... }
class LineReader {
public:
    // sizeHint is the stream size in bytes (0 = unknown: progress then stays at 0 until the end).
    // maxLineBytes is clamped to the buffer size minus room for the line end.
    LineReader(std::istream& in, std::uintmax_t sizeHint, std::uintmax_t maxBytes,
               std::size_t maxLineBytes, ProgressFn progress = {},
               const CancelToken* cancel = nullptr, std::string stage = "Reading");

    LineReader(const LineReader&) = delete;
    LineReader& operator=(const LineReader&) = delete;

    // Up to n raw bytes from the start of the stream (BOM included), without consuming them.
    // Only valid before the first next(); used to sniff binary formats.
    [[nodiscard]] std::string_view peek(std::size_t n);

    [[nodiscard]] LineStatus next(std::string_view& line);

    // 1-based number of the line last returned (or of the line that failed); 0 before any line.
    [[nodiscard]] std::size_t lineNumber() const noexcept { return lineNumber_; }
    [[nodiscard]] std::uintmax_t bytesRead() const noexcept { return bytesRead_; }
    [[nodiscard]] std::size_t maxLineBytes() const noexcept { return maxLineBytes_; }
    // bytesRead / sizeHint, clamped to [0, 1].
    [[nodiscard]] double fraction() const noexcept;

private:
    // Moves unread bytes to the front and reads more. Returns Line when bytes were added or the
    // stream simply ended (eof_ is then set), otherwise the failure status.
    LineStatus refill();

    std::istream& in_;
    std::uintmax_t sizeHint_;
    std::uintmax_t maxBytes_;
    std::size_t maxLineBytes_;
    ProgressFn progress_;
    const CancelToken* cancel_;
    std::string stage_;

    std::vector<char> buffer_;
    std::size_t begin_ = 0; // first unread byte
    std::size_t scan_ = 0;  // bytes in [begin_, scan_) are known to contain no '\n'
    std::size_t end_ = 0;   // one past the last valid byte
    bool eof_ = false;
    bool started_ = false;                  // BOM handled
    LineStatus failure_ = LineStatus::Line; // sticky refill failure
    std::uintmax_t bytesRead_ = 0;
    std::size_t lineNumber_ = 0;
};

} // namespace uniforge::io
