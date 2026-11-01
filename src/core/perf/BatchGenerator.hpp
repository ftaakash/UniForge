#pragma once

#include <cstdint>
#include <vector>

#include "core/common/Result.hpp"
#include "core/thermal/RodProblem.hpp"

namespace uniforge::perf {

// Ranges of the varied parameters (spec §7.4). Everything else keeps the brief's values
// (L = 1 m, A = 1e-4 m², T0 = 100 °C, TL = 20 °C).
inline constexpr double kBatchKMin = 5.0;   // W/(m·K)
inline constexpr double kBatchKMax = 50.0;  // W/(m·K)
inline constexpr double kBatchQMin = 1e3;   // W/m³
inline constexpr double kBatchQMax = 5e4;   // W/m³
inline constexpr int kMaxBatchCases = 1'000'000;

// Task D workload: `cases` independent rod problems with N = `elements`, k ~ U[5, 50) and
// q ~ U[1e3, 5e4) drawn from Rng(seed) in case order (k then q per case). The same seed gives
// the same batch on every compiler and platform (spec §5.2).
// Errors: InvalidInput when cases or elements is out of range (1..kMaxBatchCases,
// 1..RodProblem::kMaxElements).
[[nodiscard]] Result<std::vector<thermal::RodProblem>> generateBatch(std::uint64_t seed,
                                                                     int cases, int elements);

} // namespace uniforge::perf
