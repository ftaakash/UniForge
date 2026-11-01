#include "app/services/ThermalStudy.hpp"

#include <chrono>
#include <cmath>
#include <utility>

#include "core/common/CsvWriter.hpp"

namespace uniforge {

namespace {

constexpr int kExactSamples = 201;

} // namespace

Result<std::shared_ptr<const ThermalStudyResult>>
runThermalStudy(const thermal::RodProblem& problem, std::vector<int> meshSizes,
                const ProgressFn& progress, const CancelToken* cancel) {
    const auto start = std::chrono::steady_clock::now();
    auto r = std::make_shared<ThermalStudyResult>();
    r->problem = problem;
    auto rows = thermal::convergenceStudy(problem, meshSizes, progress, cancel, &r->meshes);
    if (!rows) {
        return std::move(rows).error();
    }
    r->rows = std::move(rows).value();
    r->meshSizes = std::move(meshSizes);
    r->exactX.resize(kExactSamples);
    r->exactT.resize(kExactSamples);
    for (int i = 0; i < kExactSamples; ++i) {
        const double x = problem.L * i / (kExactSamples - 1);
        r->exactX[static_cast<std::size_t>(i)] = x;
        r->exactT[static_cast<std::size_t>(i)] = thermal::exactTemperature(problem, x);
    }
    r->exactPeak = thermal::exactPeak(problem);
    r->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return std::shared_ptr<const ThermalStudyResult>(std::move(r));
}

Result<void> exportConvergenceCsv(const ThermalStudyResult& r, const std::filesystem::path& file) {
    CsvWriter csv({"N", "h_m", "max_mid_err_C", "expected_qh2_8k_C", "ratio", "order",
                   "max_nodal_err_C", "energy_residual_W"});
    for (const auto& row : r.rows) {
        csv.beginRow();
        csv.add(row.N);
        csv.add(row.h);
        csv.add(row.maxMidErr);
        csv.add(row.expected);
        if (std::isfinite(row.ratioToPrev)) {
            csv.add(row.ratioToPrev);
            csv.add(row.order);
        } else {
            csv.addBlank();
            csv.addBlank();
        }
        csv.add(row.maxNodalErr);
        csv.add(row.energyResidual_W);
    }
    return csv.save(file);
}

Result<void> exportNodesCsv(const ThermalStudyResult& r, const std::filesystem::path& file) {
    CsvWriter csv({"N", "x_m", "T_fe_C", "T_exact_C", "abs_err_C"});
    for (std::size_t m = 0; m < r.meshes.size(); ++m) {
        const auto& sol = r.meshes[m].solution;
        const auto& rep = r.meshes[m].report;
        for (std::size_t i = 0; i < sol.x.size(); ++i) {
            csv.beginRow();
            csv.add(r.rows[m].N);
            csv.add(sol.x[i]);
            csv.add(sol.T[i]);
            csv.add(rep.nodalExact[i]);
            csv.add(std::fabs(sol.T[i] - rep.nodalExact[i]));
        }
    }
    return csv.save(file);
}

Result<void> exportMidpointsCsv(const ThermalStudyResult& r, const std::filesystem::path& file) {
    CsvWriter csv({"N", "x_mid_m", "T_fe_mid_C", "T_exact_mid_C", "abs_err_C"});
    for (std::size_t m = 0; m < r.meshes.size(); ++m) {
        const auto& rep = r.meshes[m].report;
        for (std::size_t i = 0; i < rep.midX.size(); ++i) {
            csv.beginRow();
            csv.add(r.rows[m].N);
            csv.add(rep.midX[i]);
            csv.add(rep.midFe[i]);
            csv.add(rep.midExact[i]);
            csv.add(std::fabs(rep.midFe[i] - rep.midExact[i]));
        }
    }
    return csv.save(file);
}

} // namespace uniforge
