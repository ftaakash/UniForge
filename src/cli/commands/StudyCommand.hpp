#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace uniforge::cli {

// `uniforge_cli study …`: the V2 experiment harness (build spec §9).
//
//   study plan    [--only E2,E3] [--dry-run] [--out DIR]   list the runs, write the manifest
//   study run     --id ID [--dry-run] [--out DIR]          one run in this process (internal)
//   study batch   [--only …] [--dry-run] [--allow-dirty] [--out DIR]
//                 every run without a result, each in its own process (resumable)
//   study collect [--out DIR]                              experiment CSVs and numbers.json
//
// DIR defaults to results/v2. `args` are the words after "study".
int runStudy(const std::vector<std::string>& args, const std::string& gitCommit, std::ostream& out,
             std::ostream& err);

} // namespace uniforge::cli
