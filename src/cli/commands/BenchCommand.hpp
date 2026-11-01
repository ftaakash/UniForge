#pragma once

#include "cli/commands/Command.hpp"

namespace uniforge::cli {

// `uniforge_cli bench`: Task D serial vs parallel benchmark (spec §7.4). Runs 1 warm-up + 5
// timed repetitions per p and writes <out>/benchmarks/bench_<UTC date>.{json,csv,md} plus a
// .meta.json sidecar. Presets: --quick (500 x 1000) and --report (10000 x 8000, the committed
// evidence); --seed/--cases/--elements/--workers override them.
// Exit codes: 0 ok, 1 error, 2 bad options, 3 serial/parallel agreement failed.
int runBench(const CommandContext& ctx);

// The quick preset written to <out>/benchmarks/quick/bench_quick.* (used by `all`).
int runQuickBench(const CommandContext& ctx);

} // namespace uniforge::cli
