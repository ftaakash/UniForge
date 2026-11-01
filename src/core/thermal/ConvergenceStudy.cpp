#include "core/thermal/ConvergenceStudy.hpp"

#include <cmath>
#include <limits>
#include <string>

#include "core/thermal/Analytical.hpp"
#include "core/thermal/Mesh1D.hpp"

namespace uniforge::thermal {

Result<std::vector<StudyRow>> convergenceStudy(const RodProblem& base,
                                               const std::vector<int>& meshSizes,
                                               const ProgressFn& progress,
                                               const CancelToken* cancel,
                                               std::vector<StudyMesh>* meshes) {
    if (meshSizes.empty()) {
        return makeError(ErrorCode::InvalidInput, "The convergence study has no mesh sizes.",
                         "List at least one N, e.g. 10, 20, 40, 80.");
    }
    for (std::size_t i = 1; i < meshSizes.size(); ++i) {
        if (meshSizes[i] <= meshSizes[i - 1]) {
            return makeError(ErrorCode::InvalidInput,
                             "Mesh sizes must increase strictly; N = " +
                                 std::to_string(meshSizes[i]) + " follows N = " +
                                 std::to_string(meshSizes[i - 1]) + ".",
                             "List N in increasing order, e.g. 10, 20, 40, 80.");
        }
    }
    for (const int n : meshSizes) {
        RodProblem p = base;
        p.elements = n;
        if (auto valid = p.validate(); !valid) {
            return std::move(valid).error();
        }
    }

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::size_t count = meshSizes.size();
    std::vector<StudyRow> rows;
    rows.reserve(count);
    if (meshes != nullptr) {
        meshes->clear();
        meshes->reserve(count);
    }
    RodWorkspace ws;
    FeaSolution solution;

    for (std::size_t i = 0; i < count; ++i) {
        if (cancel != nullptr && cancel->cancelled()) {
            return makeError(ErrorCode::Cancelled,
                             "Convergence study cancelled after " + std::to_string(i) + " of " +
                                 std::to_string(count) + " meshes.",
                             "Run it again when you are ready; no results were changed.");
        }
        RodProblem p = base;
        p.elements = meshSizes[i];
        if (auto solved = solveRod(p, ws, solution, cancel); !solved) {
            return std::move(solved).error();
        }
        auto report = analyse(p, solution);
        if (!report) {
            return std::move(report).error();
        }

        StudyRow row;
        row.N = p.elements;
        row.h = Mesh1D{p.L, p.elements}.h();
        row.maxMidErr = report.value().maxMidErr;
        row.expected = expectedMidpointError(p, row.h);
        row.maxNodalErr = report.value().maxNodalErr;
        row.energyResidual_W = report.value().energyResidual_W;
        row.ratioToPrev = nan;
        row.order = nan;
        if (i > 0) {
            const StudyRow& prev = rows.back();
            row.ratioToPrev = prev.maxMidErr / row.maxMidErr;
            row.order = std::log(row.ratioToPrev) / std::log(prev.h / row.h);
        }
        rows.push_back(row);
        if (meshes != nullptr) {
            meshes->push_back({solution, std::move(report).value()});
        }
        if (progress) {
            progress(static_cast<double>(i + 1) / static_cast<double>(count),
                     "N = " + std::to_string(row.N) + " (" + std::to_string(i + 1) + "/" +
                         std::to_string(count) + ")");
        }
    }
    return rows;
}

} // namespace uniforge::thermal
