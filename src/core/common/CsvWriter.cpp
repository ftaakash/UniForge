#include "core/common/CsvWriter.hpp"

#include <cmath>
#include <cstdio>

#include "core/common/AtomicFile.hpp"

namespace uniforge {

namespace {

bool isPlainAscii(std::string_view text) {
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c > 0x7E || ch == ',' || ch == '"') {
            return false;
        }
    }
    return true;
}

} // namespace

std::string formatFullPrecision(double value) {
    char buffer[32];
    const int n = std::snprintf(buffer, sizeof buffer, "%.17g", value);
    return std::string(buffer, n > 0 ? static_cast<std::size_t>(n) : 0U);
}

CsvWriter::CsvWriter(std::vector<std::string> columns) : columns_(columns.size()) {
    if (columns.empty()) {
        fail(ErrorCode::Internal, "CSV has no columns");
    }
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (columns[i].empty() || !isPlainAscii(columns[i])) {
            fail(ErrorCode::Internal, "CSV column name " + std::to_string(i + 1) +
                                          " is empty or not plain ASCII");
        }
        text_ += (i == 0 ? "" : ",") + columns[i];
    }
    text_ += '\n';
}

void CsvWriter::beginRow() {
    finishRow();
    rowOpen_ = true;
    cellsInRow_ = 0;
}

void CsvWriter::add(double value) {
    if (!std::isfinite(value)) {
        fail(ErrorCode::NonFinite, "CSV row " + std::to_string(rows_ + 1) + ", column " +
                                       std::to_string(cellsInRow_ + 1) + " is not finite");
    }
    addCell(formatFullPrecision(value));
}

void CsvWriter::add(int value) { addCell(std::to_string(value)); }

void CsvWriter::add(long long value) { addCell(std::to_string(value)); }

void CsvWriter::add(std::string_view text) {
    if (!isPlainAscii(text)) {
        fail(ErrorCode::Internal, "CSV row " + std::to_string(rows_ + 1) +
                                      " has text that is not plain ASCII or contains , or \"");
    }
    addCell(text);
}

void CsvWriter::addBlank() { addCell({}); }

void CsvWriter::addCell(std::string_view cell) {
    if (!rowOpen_) {
        fail(ErrorCode::Internal, "CSV cell added before beginRow()");
        beginRow();
    }
    if (cellsInRow_ > 0) {
        text_ += ',';
    }
    text_ += cell;
    ++cellsInRow_;
}

void CsvWriter::finishRow() {
    if (!rowOpen_) {
        return;
    }
    ++rows_;
    if (cellsInRow_ != columns_) {
        fail(ErrorCode::Internal, "CSV row " + std::to_string(rows_) + " has " +
                                      std::to_string(cellsInRow_) + " cells, expected " +
                                      std::to_string(columns_));
    }
    text_ += '\n';
    rowOpen_ = false;
}

void CsvWriter::fail(ErrorCode code, std::string message) {
    if (!firstError_) {
        firstError_ = makeError(code, std::move(message),
                                "This is a bug in the exporting code; please report it.");
    }
}

Result<void> CsvWriter::validate() const {
    if (firstError_) {
        return *firstError_;
    }
    if (rowOpen_ && cellsInRow_ != columns_) {
        return makeError(ErrorCode::Internal,
                         "CSV row " + std::to_string(rows_ + 1) + " has " +
                             std::to_string(cellsInRow_) + " cells, expected " +
                             std::to_string(columns_),
                         "This is a bug in the exporting code; please report it.");
    }
    return {};
}

std::string CsvWriter::str() const {
    std::string out = text_;
    if (rowOpen_) {
        out += '\n';
    }
    return out;
}

Result<void> CsvWriter::save(const std::filesystem::path& path) const {
    if (auto valid = validate(); !valid) {
        return valid;
    }
    return writeFileAtomic(path, str());
}

} // namespace uniforge
