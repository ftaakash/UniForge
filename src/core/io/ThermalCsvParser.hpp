#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>
#include <string_view>
#include <vector>

#include "core/common/CancelToken.hpp"
#include "core/common/Progress.hpp"
#include "core/common/Result.hpp"
#include "core/io/ImportLimits.hpp"
#include "core/io/ImportTypes.hpp"

namespace uniforge::io {

// The only accepted header (spec B2). Units are part of the column names: metres and °C.
inline constexpr std::string_view kThermalCsvHeader = "node_id,x_m,y_m,z_m,temperature_C";
inline constexpr double kAbsoluteZero_C = -273.15;

struct ThermalNode {
    std::int64_t id = 0;
    Vec3d p{};        // m
    double T_C = 0.0; // °C
};

// Nodal temperatures, e.g. exported from ANSYS Mechanical (spec §5.3).
struct ThermalNodeSet {
    std::vector<ThermalNode> nodes; // file order
    double Tmin = 0.0;              // °C
    double Tmax = 0.0;              // °C
    Vec3d bbMin{};
    Vec3d bbMax{};
    bool syntheticFlag = false; // file name contains "SYNTHETIC" (set by parseThermalCsvFile)
    ImportStats stats;
};

// Parses a nodal-temperature CSV (spec §7.2):
//   - optional UTF-8 BOM, LF or CRLF line ends, blank lines ignored;
//   - first line exactly `node_id,x_m,y_m,z_m,temperature_C` (spaces around names allowed);
//   - every data row: integer node_id and four finite decimal numbers, spaces around values
//     allowed, no quotes.
// Failures are typed errors with a 1-based line where one applies:
//   EmptyFile      0 bytes / only blank lines, or a header without data rows ("No data rows")
//   ParseError     wrong header, missing or extra columns, quoted fields, empty or non-numeric
//                  values, non-integer node_id, a line over maxLineBytes
//   NonFinite      nan / inf, or a number outside the double range
//   InvalidInput   duplicate node_id, temperature below absolute zero (-273.15 °C)
//   LimitExceeded  more than limits.maxNodes rows or limits.maxBytes bytes
//   Cancelled, IoError
[[nodiscard]] Result<ThermalNodeSet> parseThermalCsv(std::istream& in, std::uintmax_t sizeHint,
                                                     const ImportLimits& limits = {},
                                                     ProgressFn progress = {},
                                                     const CancelToken* cancel = nullptr);

// Opens `path` and parses it; sets syntheticFlag from the file name.
[[nodiscard]] Result<ThermalNodeSet> parseThermalCsvFile(const std::filesystem::path& path,
                                                         const ImportLimits& limits = {},
                                                         ProgressFn progress = {},
                                                         const CancelToken* cancel = nullptr);

// True when the file name (not the folder) contains "SYNTHETIC" in any letter case, the naming
// rule for self-generated data that must not be mistaken for an ANSYS export (spec B4).
[[nodiscard]] bool isSyntheticName(const std::filesystem::path& path);

} // namespace uniforge::io
