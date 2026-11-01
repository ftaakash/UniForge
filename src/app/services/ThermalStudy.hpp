#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include "core/common/Result.hpp"
#include "core/thermal/Analytical.hpp"
#include "core/thermal/ConvergenceStudy.hpp"

namespace uniforge {

// Everything the Thermal Analysis view shows for one run of the convergence study (Task C).
// Produced on the job thread, published to ResultStore::thermal, then only read.
struct ThermalStudyResult {
    thermal::RodProblem problem;            // elements field unused (see meshSizes)
    std::vector<int> meshSizes;             // N per row, ascending
    std::vector<thermal::StudyRow> rows;    // one per mesh size
    std::vector<thermal::StudyMesh> meshes; // nodal solution + error report per mesh size
    std::vector<double> exactX, exactT;     // dense exact curve for the plot (m, °C)
    thermal::TemperaturePeak exactPeak;     // analytical T_max and its position
    double seconds = 0.0;                   // wall time of the study
};

// Runs thermal::convergenceStudy and samples the exact curve. Errors are the core's typed
// errors (invalid parameters, Cancelled).
[[nodiscard]] Result<std::shared_ptr<const ThermalStudyResult>>
runThermalStudy(const thermal::RodProblem& problem, std::vector<int> meshSizes,
                const ProgressFn& progress = {}, const CancelToken* cancel = nullptr);

// CSV exports (spec §7.3 column names, %.17g numbers, atomic temp → rename via CsvWriter).
// convergence.csv:
// N,h_m,max_mid_err_C,expected_qh2_8k_C,ratio,order,max_nodal_err_C,energy_residual_W
Result<void> exportConvergenceCsv(const ThermalStudyResult& r, const std::filesystem::path& file);
// All meshes in one long table: N,x_m,T_fe_C,T_exact_C,abs_err_C
Result<void> exportNodesCsv(const ThermalStudyResult& r, const std::filesystem::path& file);
// All meshes: N,x_mid_m,T_fe_mid_C,T_exact_mid_C,abs_err_C
Result<void> exportMidpointsCsv(const ThermalStudyResult& r, const std::filesystem::path& file);

} // namespace uniforge
