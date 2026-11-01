#pragma once

#include "cli/commands/Command.hpp"

namespace uniforge::cli {

// `uniforge_cli import-bench [--sizes 10000,100000,1000000] [--out DIR]`: measures the importers
// (spec §7.2, B3). For every size n it writes a temporary ASCII STL with n facets and a CSV with
// n nodes, parses each in a fresh child process (`import-bench-worker`, so every peak-memory
// reading starts from zero), deletes the file, and writes
//   DIR/benchmarks/import_limits.csv (+ .meta.json) and DIR/benchmarks/import_limits.md
// with file size, parse time (median of 3), throughput and peak memory per run.
int runImportBench(const CommandContext& ctx);

// Internal: `import-bench-worker <stl|csv> FILE RESULT` parses FILE three times without limits
// and writes key=value results to RESULT.
int runImportBenchWorker(const CommandContext& ctx);

} // namespace uniforge::cli
