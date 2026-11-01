#pragma once

#include "cli/commands/Command.hpp"

namespace uniforge::cli {

// Seed of the synthetic temperature noise (documented in data/PROVENANCE.md).
inline constexpr unsigned long long kSampleSeed = 20261101ULL;

// `uniforge_cli gen-samples`: writes the Task B sample files (spec §7.2), all self-created and
// byte-identical on every run:
//   <data>/geometry/rod_10x10mm_1m.stl                  the Task C rod, 1 m x 10 mm x 10 mm
//   <data>/geometry/heatsink_fins.stl                   a small finned heat sink
//   <data>/thermal/rod_nodal_temperature_SYNTHETIC.csv  T(x) of Task C on nodes of the rod,
//                                                        plus a small lateral variation
// then parses each file back and prints PASS/FAIL checks (counts, bbox, enclosed volume,
// temperature range).
int runGenSamples(const CommandContext& ctx);

// `uniforge_cli inspect FILE...`: imports each .stl / .csv file with the GUI's parsers and
// prints the summary, the warnings, or the error with its code, line and hint. Exit code 1 if
// any file is rejected.
int runInspect(const CommandContext& ctx);

} // namespace uniforge::cli
