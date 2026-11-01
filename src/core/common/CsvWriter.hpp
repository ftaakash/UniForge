#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/common/Result.hpp"

namespace uniforge {

// Formats a double with 17 significant digits ("%.17g"), enough to round-trip any double.
// Uses the C locale's '.' decimal point (the core never calls setlocale).
[[nodiscard]] std::string formatFullPrecision(double value);

// Builds a plain ASCII CSV (comma separator, LF line ends, header row, no quoting) in memory
// and saves it atomically. Evidence files must be byte-identical on rerun, so numbers are
// always written with formatFullPrecision.
//
//     CsvWriter csv({"x_m", "T_C"});
//     csv.beginRow(); csv.add(0.5); csv.add(188.0);
//     auto saved = csv.save("results/csv/out.csv");
//
// Problems found while adding cells (non-finite number, wrong cell count, non-ASCII or
// separator characters in text) are remembered and reported by save() / validate(), so the
// building code stays free of error checks.
class CsvWriter {
public:
    explicit CsvWriter(std::vector<std::string> columns);

    void beginRow();
    void add(double value);
    void add(int value);
    void add(long long value);
    void add(std::string_view text);
    void addBlank(); // empty cell, e.g. a ratio that is undefined for the first row

    [[nodiscard]] std::size_t rowCount() const noexcept { return rows_ + (rowOpen_ ? 1U : 0U); }
    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::string str() const;
    Result<void> save(const std::filesystem::path& path) const;

private:
    void addCell(std::string_view cell);
    void finishRow();
    void fail(ErrorCode code, std::string message);

    std::size_t columns_ = 0;
    std::size_t rows_ = 0;
    std::size_t cellsInRow_ = 0;
    bool rowOpen_ = false;
    std::string text_;
    std::optional<Error> firstError_;
};

} // namespace uniforge
