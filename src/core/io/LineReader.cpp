#include "core/io/LineReader.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

#include "core/io/ImportLimits.hpp"

namespace uniforge::io {

LineReader::LineReader(std::istream& in, std::uintmax_t sizeHint, std::uintmax_t maxBytes,
                       std::size_t maxLineBytes, ProgressFn progress, const CancelToken* cancel,
                       std::string stage)
    : in_(in)
    , sizeHint_(sizeHint)
    , maxBytes_(maxBytes)
    ,
    // Leave room for "\r\n" so a line of exactly maxLineBytes always fits with its line end.
    maxLineBytes_(std::min(maxLineBytes, kLineReaderBufferBytes - 2))
    , progress_(std::move(progress))
    , cancel_(cancel)
    , stage_(std::move(stage))
    , buffer_(kLineReaderBufferBytes) {}

double LineReader::fraction() const noexcept {
    if (sizeHint_ == 0) {
        return 0.0;
    }
    const double f = static_cast<double>(bytesRead_) / static_cast<double>(sizeHint_);
    return f < 1.0 ? f : 1.0;
}

LineStatus LineReader::refill() {
    if (failure_ != LineStatus::Line) {
        return failure_; // a failure seen by peek() is reported again by next()
    }
    if (cancel_ != nullptr && cancel_->cancelled()) {
        return failure_ = LineStatus::Cancelled;
    }
    if (begin_ > 0) {
        std::memmove(buffer_.data(), buffer_.data() + begin_, end_ - begin_);
        scan_ -= begin_;
        end_ -= begin_;
        begin_ = 0;
    }
    const std::size_t room = buffer_.size() - end_;
    if (room == 0 || eof_) {
        return LineStatus::Line;
    }
    in_.read(buffer_.data() + end_, static_cast<std::streamsize>(room));
    if (in_.bad()) {
        return failure_ = LineStatus::ReadError;
    }
    const auto got = static_cast<std::size_t>(in_.gcount());
    if (got < room) {
        eof_ = true; // read() sets eofbit (and failbit) on a short read
    }
    end_ += got;
    bytesRead_ += got;
    if (bytesRead_ > maxBytes_) {
        return failure_ = LineStatus::TooLarge;
    }
    if (progress_) {
        progress_(fraction(), stage_);
    }
    return LineStatus::Line;
}

std::string_view LineReader::peek(std::size_t n) {
    n = std::min(n, buffer_.size());
    while (end_ - begin_ < n && !eof_) {
        if (refill() != LineStatus::Line) {
            break;
        }
    }
    return {buffer_.data() + begin_, std::min(n, end_ - begin_)};
}

LineStatus LineReader::next(std::string_view& line) {
    if (failure_ != LineStatus::Line) {
        return failure_;
    }
    for (;;) {
        if (!started_ && (end_ - begin_ >= 3 || eof_)) {
            started_ = true;
            if (end_ - begin_ >= 3 &&
                std::memcmp(buffer_.data() + begin_, "\xEF\xBB\xBF", 3) == 0) {
                begin_ += 3;
                scan_ = begin_;
            }
        }
        if (started_) {
            scan_ = std::max(scan_, begin_);
            const char* first = buffer_.data() + scan_;
            const auto* nl = static_cast<const char*>(std::memchr(first, '\n', end_ - scan_));
            std::size_t lineEnd = 0;
            std::size_t next = 0;
            if (nl != nullptr) {
                lineEnd = static_cast<std::size_t>(nl - buffer_.data());
                next = lineEnd + 1;
            } else if (eof_) {
                if (begin_ == end_) {
                    return LineStatus::End;
                }
                lineEnd = end_; // last line without a line end
                next = end_;
            } else {
                scan_ = end_;
                if (end_ - begin_ > maxLineBytes_ + 1) { // +1: a pending '\r'
                    ++lineNumber_;
                    return LineStatus::TooLong;
                }
            }
            if (next != 0) {
                std::size_t length = lineEnd - begin_;
                if (length > 0 && buffer_[begin_ + length - 1] == '\r') {
                    --length;
                }
                line = std::string_view(buffer_.data() + begin_, length);
                begin_ = next;
                scan_ = next;
                ++lineNumber_;
                return length > maxLineBytes_ ? LineStatus::TooLong : LineStatus::Line;
            }
        }
        if (const LineStatus s = refill(); s != LineStatus::Line) {
            return s;
        }
    }
}

} // namespace uniforge::io
