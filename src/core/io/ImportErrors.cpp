#include "core/io/ImportErrors.hpp"

#include <system_error>
#include <utility>

namespace uniforge::io::detail {

Error lineError(ErrorCode code, std::size_t line, std::string_view what, std::string hint) {
    Error e =
        makeError(code, "Line " + std::to_string(line) + ": " + std::string(what), std::move(hint));
    e.line = line;
    return e;
}

Error readerError(LineStatus status, const LineReader& reader, const ImportLimits& limits,
                  std::string_view kind) {
    const std::string k(kind);
    switch (status) {
    case LineStatus::TooLong:
        return lineError(ErrorCode::ParseError, reader.lineNumber(),
                         "the line is longer than " + formatCount(reader.maxLineBytes()) +
                             " characters.",
                         "A valid " + k +
                             " has short text lines. The file is probably binary or damaged; "
                             "re-export it.");
    case LineStatus::TooLarge:
        return makeError(ErrorCode::LimitExceeded,
                         "The " + k + " is larger than the import limit of " +
                             formatMegabytes(limits.maxBytes) + ".",
                         "UniForge is tested up to " + formatMegabytes(limits.maxBytes) +
                             " per file (results/benchmarks/import_limits.md). Split or "
                             "simplify the data.");
    case LineStatus::Cancelled:
        return makeError(ErrorCode::Cancelled, "Import cancelled.",
                         "The previously loaded data is kept.");
    case LineStatus::ReadError:
        return makeError(ErrorCode::IoError,
                         "Reading the " + k + " failed after " + formatCount(reader.bytesRead()) +
                             " bytes.",
                         "Check that the file is still available (removable drive, network "
                         "share) and try again.");
    case LineStatus::Line:
    case LineStatus::End:
        break;
    }
    return makeError(ErrorCode::Internal, "Unexpected line reader state.", "Please report this.");
}

Error fileTooLargeError(std::uintmax_t size, const ImportLimits& limits, std::string_view kind) {
    return makeError(ErrorCode::LimitExceeded,
                     "The " + std::string(kind) + " is " + formatMegabytes(size) +
                         "; the import limit is " + formatMegabytes(limits.maxBytes) + ".",
                     "UniForge is tested up to " + formatMegabytes(limits.maxBytes) +
                         " per file (results/benchmarks/import_limits.md). Split or simplify "
                         "the data.");
}

Error numberError(NumberStatus status, std::size_t line, std::string_view field,
                  std::string_view token, std::string_view numberHint) {
    const std::string quoted = "`" + quoteForMessage(token, 40) + "`";
    switch (status) {
    case NumberStatus::NonFinite:
        return lineError(ErrorCode::NonFinite, line,
                         std::string(field) + " " + quoted + " is not a finite number.",
                         "NaN and infinite values cannot be displayed. Fix the value or "
                         "re-export the data.");
    case NumberStatus::OutOfRange:
        return lineError(ErrorCode::NonFinite, line,
                         std::string(field) + " " + quoted +
                             " is too large or too small to store as a double.",
                         "Values must lie within about 1e-308 to 1e308 in magnitude. Check the "
                         "unit and re-export the data.");
    case NumberStatus::Invalid:
    case NumberStatus::Ok:
        break;
    }
    if (token.empty()) {
        return lineError(ErrorCode::ParseError, line, std::string(field) + " is empty.",
                         "Every value must be filled in. Check for a doubled separator or a "
                         "missing number.");
    }
    return lineError(ErrorCode::ParseError, line,
                     std::string(field) + " " + quoted + " is not a number.",
                     std::string(numberHint));
}

Result<std::uintmax_t> openForImport(const std::filesystem::path& path, std::ifstream& stream) {
    const std::string name = quoteForMessage(path.filename().u8string(), 80);
    std::error_code ec;
    const auto status = std::filesystem::status(path, ec);
    if (ec || !std::filesystem::exists(status)) {
        return makeError(ErrorCode::IoError, "Cannot open " + name + ": the file does not exist.",
                         "Check the file name and folder, then choose the file again.");
    }
    if (std::filesystem::is_directory(status)) {
        return makeError(ErrorCode::IoError, "Cannot open " + name + ": it is a folder.",
                         "Choose a file inside the folder.");
    }
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    stream.open(path, std::ios::binary);
    if (ec || !stream.is_open()) {
        return makeError(ErrorCode::IoError, "Cannot open " + name + " for reading.",
                         "Check that the file is not locked by another program and that you "
                         "have permission to read it.");
    }
    return size;
}

} // namespace uniforge::io::detail
