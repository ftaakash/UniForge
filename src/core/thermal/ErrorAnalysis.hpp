#pragma once

#include <cstddef>
#include <vector>

#include "core/common/Result.hpp"
#include "core/thermal/RodProblem.hpp"
#include "core/thermal/RodSolver.hpp"

namespace uniforge::thermal {

struct ErrorReport {
    std::vector<double> nodalExact; // T_exact at each node, °C (size N + 1)
    double maxNodalErr = 0.0;       // max |T_fe − T_exact| over the nodes, °C
    std::vector<double> midX;       // element midpoints, m (size N)
    std::vector<double> midFe;      // FE interpolation ½(T_i + T_{i+1}) at the midpoints, °C
    std::vector<double> midExact;   // T_exact at the midpoints, °C
    double maxMidErr = 0.0;         // max |midFe − midExact|, °C
    double energyResidual_W = 0.0;  // heatOutLeft + heatOutRight − q·A·L, W
};

// Compares an FE solution with the exact solution (spec §5.2). The FE field between two nodes
// is the linear interpolation, so its value at an element midpoint is the nodal average.
// Errors: InvalidInput when `solution` does not belong to `problem` (wrong sizes).
[[nodiscard]] Result<ErrorReport> analyse(const RodProblem& problem, const FeaSolution& solution);

struct NodalPeak {
    std::size_t node = 0;
    double x = 0.0; // m
    double T = 0.0; // °C
};

// Hottest node of an FE solution (the first one on ties). Pre-condition: solution not empty.
[[nodiscard]] NodalPeak nodalPeak(const FeaSolution& solution) noexcept;

} // namespace uniforge::thermal
