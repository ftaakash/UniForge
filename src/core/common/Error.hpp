#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace uniforge {

// Error taxonomy shared by every core module (spec §5.1). The UI shows `message` and `hint`
// verbatim and never invents its own text, so both should be complete sentences.
enum class ErrorCode {
    InvalidInput,
    EmptyFile,
    ParseError,
    NonFinite,
    LimitExceeded,
    UnsupportedFormat,
    IoError,
    Cancelled,
    DeviceNotFound,
    DeviceTimeout,
    DeviceFault,
    Internal
};

struct Error {
    ErrorCode code = ErrorCode::Internal;
    std::string message;             // what happened + where
    std::string hint;                // what the user should do
    std::optional<std::size_t> line; // 1-based, for parsers
    bool retryable = false;
};

// Stable identifier for logs and CLI output, e.g. "InvalidInput".
[[nodiscard]] constexpr std::string_view toString(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::InvalidInput:
        return "InvalidInput";
    case ErrorCode::EmptyFile:
        return "EmptyFile";
    case ErrorCode::ParseError:
        return "ParseError";
    case ErrorCode::NonFinite:
        return "NonFinite";
    case ErrorCode::LimitExceeded:
        return "LimitExceeded";
    case ErrorCode::UnsupportedFormat:
        return "UnsupportedFormat";
    case ErrorCode::IoError:
        return "IoError";
    case ErrorCode::Cancelled:
        return "Cancelled";
    case ErrorCode::DeviceNotFound:
        return "DeviceNotFound";
    case ErrorCode::DeviceTimeout:
        return "DeviceTimeout";
    case ErrorCode::DeviceFault:
        return "DeviceFault";
    case ErrorCode::Internal:
        return "Internal";
    }
    return "Unknown";
}

[[nodiscard]] inline Error makeError(ErrorCode code, std::string message, std::string hint = {}) {
    Error e;
    e.code = code;
    e.message = std::move(message);
    e.hint = std::move(hint);
    return e;
}

} // namespace uniforge
