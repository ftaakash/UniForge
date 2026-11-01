#pragma once

#include <vector>

#include "core/common/CancelToken.hpp"
#include "core/common/Progress.hpp"
#include "core/common/Result.hpp"
#include "core/thermal/ErrorAnalysis.hpp"
#include "core/thermal/RodProblem.hpp"
#include "core/thermal/RodSolver.hpp"

namespace uniforge::thermal {

struct StudyRow {
    int N = 0;
    double h = 0.0;                // element length, m
    double maxMidErr = 0.0;        // measured max midpoint error, °C
    double expected = 0.0;         // q·h²/(8k), °C
    double ratioToPrev = 0.0;      // previous row's maxMidErr / this row's (NaN in row 1)
    double order = 0.0;            // observed order log(ratio)/log(h_prev/h) (NaN in row 1)
    double maxNodalErr = 0.0;      // °C
    double energyResidual_W = 0.0; // W
};

// Full result for one mesh of the study, for exporting the per-mesh CSVs.
struct StudyMesh {
    FeaSolution solution;
    ErrorReport report;
};

// Solves `base` once per entry of `meshSizes` (base.elements is ignored) and tabulates the
// midpoint error, its expected value and the observed convergence order (spec §5.2).
// `progress` is called after each mesh with the fraction done and "N = 40 (3/4)";
// cancellation is checked before each mesh. When `meshes` is non-null it receives one entry per
// mesh size.
// Errors: InvalidInput (empty list, or sizes not strictly increasing), the problem's validate()
// errors (also N < 1 or N too large), Cancelled.
[[nodiscard]] Result<std::vector<StudyRow>>
convergenceStudy(const RodProblem& base, const std::vector<int>& meshSizes,
                 const ProgressFn& progress = {}, const CancelToken* cancel = nullptr,
                 std::vector<StudyMesh>* meshes = nullptr);

} // namespace uniforge::thermal
