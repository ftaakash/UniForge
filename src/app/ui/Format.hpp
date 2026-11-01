#pragma once

#include <cstdint>
#include <string>

// Number formatting for everything the UI shows (spec §8.3). Pure C++ with no ImGui
// dependency, so the rules are unit-tested (tests/unit/ui/FormatTest.cpp). Units follow the
// value after one space; NaN and infinity print as "—" so a missing value never looks like data.
namespace uniforge::ui {

// 4 decimals: "188.2000 °C".
[[nodiscard]] std::string formatTemperature(double celsius);

// FE errors, 7 significant digits: fixed between 1 and 1e5 ("1.250000 °C"), scientific
// otherwise ("1.953125e-02 °C", "0.000000e+00 °C").
[[nodiscard]] std::string formatFeError(double celsius);

// Ratio or convergence order, 4 decimals: "4.0000".
[[nodiscard]] std::string formatRatio(double value);

// Duration, 3 significant digits with an automatic unit: "412 ms", "1.23 s", "18.0 µs".
[[nodiscard]] std::string formatDuration(double seconds);

// "3.71×" and "92.8 %" (efficiency given as a fraction, 0.928).
[[nodiscard]] std::string formatSpeedup(double speedup);
[[nodiscard]] std::string formatEfficiency(double fraction);

// Agreement against a tolerance: "0.00e+00 °C (≤ 1e-08 ✓)" or "(> 1e-08 ✕)".
[[nodiscard]] std::string formatAgreement(double diffCelsius, double tolerance);

// Coordinate, 6 significant digits, trailing zeros kept: "0.420000 m", "1.00000 m".
[[nodiscard]] std::string formatCoordinate(double metres);

// Thousands separators: "1,234,567".
[[nodiscard]] std::string formatCount(std::uint64_t count);

// "512 B", "24.8 KB", "1.50 MB" (decimal units, 3 significant digits).
[[nodiscard]] std::string formatBytes(std::uint64_t bytes);

// Short scientific number for tolerances: "1e-09", "2.5e-03".
[[nodiscard]] std::string formatTolerance(double value);

} // namespace uniforge::ui
