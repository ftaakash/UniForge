#include "core/io/ThermalCsvParser.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <numeric>
#include <string>
#include <utility>

#include "core/io/ImportErrors.hpp"
#include "core/io/LineReader.hpp"
#include "core/io/TextParse.hpp"

namespace uniforge::io {

namespace {

using detail::lineError;

constexpr std::string_view kKind = "CSV file";
constexpr std::size_t kColumns = 5;
constexpr std::string_view kColumnNames[kColumns] = {"node_id", "x_m", "y_m", "z_m",
                                                     "temperature_C"};
constexpr std::string_view kNumberHint =
    "Use plain decimal numbers with '.' as the decimal separator, e.g. 0.125 or 1.5e-3.";

bool isBlank(std::string_view line) noexcept {
    return trimBlanks(line).empty();
}

// Splits on ',' into at most kColumns trimmed cells; returns the total number of cells.
std::size_t splitRow(std::string_view line, std::string_view (&cells)[kColumns]) noexcept {
    std::size_t count = 0;
    std::size_t start = 0;
    for (;;) {
        const std::size_t comma = line.find(',', start);
        const std::string_view cell = line.substr(
            start, comma == std::string_view::npos ? line.size() - start : comma - start);
        if (count < kColumns) {
            cells[count] = trimBlanks(cell);
        }
        ++count;
        if (comma == std::string_view::npos) {
            return count;
        }
        start = comma + 1;
    }
}

Error headerError(std::size_t line, std::string_view text) {
    std::string hint = "The first line must name the five columns exactly as shown, in this "
                       "order and separated by commas (units are part of the names).";
    if (text.find(';') != std::string_view::npos && text.find(',') == std::string_view::npos) {
        hint = "The file uses ';' as separator (a regional spreadsheet setting). Save it with ',' "
               "separators and '.' decimals.";
    } else if (text.find('"') != std::string_view::npos) {
        hint = "Remove the double quotes around the column names.";
    } else if (!text.empty() &&
               ((text.front() >= '0' && text.front() <= '9') || text.front() == '-')) {
        hint = "The file has no header row. Add `" + std::string(kThermalCsvHeader) +
               "` as the first line.";
    }
    return lineError(ErrorCode::ParseError, line,
                     "the header must be `" + std::string(kThermalCsvHeader) + "` but is `" +
                         quoteForMessage(text) + "`.",
                     std::move(hint));
}

std::string formatTemperature(double t) {
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "%.6g", t);
    return buffer;
}

} // namespace

Result<ThermalNodeSet> parseThermalCsv(std::istream& in, std::uintmax_t sizeHint,
                                       const ImportLimits& limits, ProgressFn progress,
                                       const CancelToken* cancel) {
    const auto started = std::chrono::steady_clock::now();
    if (sizeHint > limits.maxBytes) {
        return detail::fileTooLargeError(sizeHint, limits, kKind);
    }
    LineReader reader(in, sizeHint, limits.maxBytes, limits.maxLineBytes, std::move(progress),
                      cancel, "Reading CSV");

    ThermalNodeSet set;
    std::vector<std::size_t> rowLine; // line of each node, for duplicate-id messages
    bool headerSeen = false;
    bool idsIncreasing = true;
    BoundsAccumulator bounds;
    std::string_view cells[kColumns];

    std::string_view line;
    LineStatus status = LineStatus::Line;
    while ((status = reader.next(line)) == LineStatus::Line) {
        if (isBlank(line)) {
            continue;
        }
        const std::size_t ln = reader.lineNumber();
        if (!headerSeen) {
            const std::size_t n = splitRow(line, cells);
            bool match = n == kColumns;
            for (std::size_t i = 0; match && i < kColumns; ++i) {
                match = cells[i] == kColumnNames[i];
            }
            if (!match) {
                return headerError(ln, trimBlanks(line));
            }
            headerSeen = true;
            continue;
        }

        if (line.find('"') != std::string_view::npos) {
            return lineError(ErrorCode::ParseError, ln,
                             "quoted fields are not supported (`" + quoteForMessage(line) + "`).",
                             "Remove the double quotes; values must be plain numbers separated "
                             "by commas.");
        }
        const std::size_t n = splitRow(line, cells);
        if (n != kColumns) {
            return lineError(ErrorCode::ParseError, ln,
                             "expected 5 values (" + std::string(kThermalCsvHeader) +
                                 ") but found " + std::to_string(n) + ".",
                             n < kColumns ? "A value is missing. Check for an empty or truncated "
                                            "row."
                                          : "There is an extra value. Check for a stray comma; "
                                            "decimals must use '.', not ','.");
        }
        if (set.nodes.size() == limits.maxNodes) {
            return lineError(ErrorCode::LimitExceeded, ln,
                             "the file has more than " + formatCount(limits.maxNodes) + " nodes.",
                             "UniForge is tested up to " + formatCount(limits.maxNodes) +
                                 " nodes (results/benchmarks/import_limits.md). Export a coarser "
                                 "mesh or a subset of the nodes.");
        }

        ThermalNode node;
        const NumberStatus idStatus = parseInt64(cells[0], node.id);
        if (idStatus != NumberStatus::Ok) {
            double asDouble = 0.0;
            const bool numeric = parseDouble(cells[0], asDouble) == NumberStatus::Ok;
            return lineError(ErrorCode::ParseError, ln,
                             cells[0].empty() ? std::string("node_id is empty.")
                                              : "node_id `" + quoteForMessage(cells[0], 40) +
                                                    (idStatus == NumberStatus::OutOfRange
                                                         ? "` is outside the 64-bit integer range."
                                                         : "` is not an integer."),
                             numeric ? "node_id must be a whole number such as 17."
                                     : "node_id must be a whole number such as 17; check that "
                                       "the columns are in the order of the header.");
        }
        double* values[4] = {&node.p.x, &node.p.y, &node.p.z, &node.T_C};
        for (std::size_t i = 0; i < 4; ++i) {
            const NumberStatus s = parseDouble(cells[i + 1], *values[i]);
            if (s != NumberStatus::Ok) {
                return detail::numberError(s, ln, std::string(kColumnNames[i + 1]) + " value",
                                           cells[i + 1], kNumberHint);
            }
        }
        if (node.T_C < kAbsoluteZero_C) {
            return lineError(ErrorCode::InvalidInput, ln,
                             "temperature_C = " + formatTemperature(node.T_C) +
                                 " is below absolute zero (-273.15 \xC2\xB0"
                                 "C).",
                             "The column must hold temperatures in \xC2\xB0"
                             "C. Check the "
                             "export unit and the value on this line.");
        }
        if (!set.nodes.empty() && node.id <= set.nodes.back().id) {
            idsIncreasing = false;
        }
        set.nodes.push_back(node);
        rowLine.push_back(ln);
        bounds.add(node.p);
        if (set.nodes.size() == 1) {
            set.Tmin = set.Tmax = node.T_C;
        } else {
            set.Tmin = std::min(set.Tmin, node.T_C);
            set.Tmax = std::max(set.Tmax, node.T_C);
        }
    }
    if (status != LineStatus::End) {
        return detail::readerError(status, reader, limits, kKind);
    }
    if (!headerSeen) {
        return makeError(ErrorCode::EmptyFile,
                         reader.bytesRead() == 0 ? "The CSV file is empty (0 bytes)."
                                                 : "The CSV file contains only blank lines.",
                         "Choose a nodal-temperature CSV with the header `" +
                             std::string(kThermalCsvHeader) + "` and at least one data row.");
    }
    if (set.nodes.empty()) {
        return makeError(ErrorCode::EmptyFile,
                         "No data rows: the CSV file has a header but no nodes.",
                         "Export the nodal temperature results, not only the column names.");
    }

    // Duplicate ids. Exports are usually sorted by id, which already rules duplicates out;
    // otherwise sort row indices by (id, line) and report the earliest repeated row.
    if (!idsIncreasing) {
        std::vector<std::size_t> order(set.nodes.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            const auto ia = set.nodes[a].id;
            const auto ib = set.nodes[b].id;
            return ia != ib ? ia < ib : a < b;
        });
        std::size_t dupRow = order.size();
        std::size_t firstRow = 0;
        const auto idAt = [&](std::size_t k) { return set.nodes[order[k]].id; };
        for (std::size_t k = 1; k < order.size(); ++k) {
            // order[k] is the first repeat of its id when order[k - 1] starts the id's group.
            const bool firstRepeat = idAt(k) == idAt(k - 1) && (k < 2 || idAt(k - 2) != idAt(k));
            if (firstRepeat && order[k] < dupRow) {
                dupRow = order[k];
                firstRow = order[k - 1];
            }
        }
        if (dupRow != order.size()) {
            return lineError(ErrorCode::InvalidInput, rowLine[dupRow],
                             "node_id " + std::to_string(set.nodes[dupRow].id) +
                                 " appears again (first on line " +
                                 std::to_string(rowLine[firstRow]) + ").",
                             "Each node must appear once. Remove the duplicate row or export "
                             "the results again.");
        }
    }

    set.bbMin = bounds.min();
    set.bbMax = bounds.max();
    set.stats.bytes = reader.bytesRead();
    set.stats.lines = reader.lineNumber();
    set.stats.parseSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return set;
}

Result<ThermalNodeSet> parseThermalCsvFile(const std::filesystem::path& path,
                                           const ImportLimits& limits, ProgressFn progress,
                                           const CancelToken* cancel) {
    std::ifstream stream;
    auto size = detail::openForImport(path, stream);
    if (!size) {
        return std::move(size).error();
    }
    auto set = parseThermalCsv(stream, size.value(), limits, std::move(progress), cancel);
    if (set) {
        set.value().syntheticFlag = isSyntheticName(path);
    }
    return set;
}

bool isSyntheticName(const std::filesystem::path& path) {
    std::string name = path.filename().u8string();
    for (char& c : name) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return name.find("SYNTHETIC") != std::string::npos;
}

} // namespace uniforge::io
