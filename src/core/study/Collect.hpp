#pragma once

#include <map>
#include <string>

#include "core/common/Result.hpp"
#include "core/study/Runner.hpp"

namespace uniforge::study {

struct CollectSummary {
    int runs = 0;
    std::map<std::string, int> perExperiment;
    std::map<std::string, int> perStatus; // solve_status (or setup_status when setup failed)
};

// Reads every results/v2/runs/*.csv and writes (build spec §9, §12.4):
//   csv/all_runs.csv, csv/<experiment>.csv        the rows, sorted by id
//   csv/E5_load_cases.csv                         setup + k·solve for k = 1, 5, 20 (from E2)
//   csv/E2_time_to_accuracy.csv                   time to 1e-3 K and to 1 % of the Richardson
//                                                 estimate of the discretisation error of T_max
//   numbers.json                                  counts (the report's numbers come in V2-10)
[[nodiscard]] Result<CollectSummary> collect(const RunPaths& paths);

} // namespace uniforge::study
