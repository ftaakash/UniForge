#pragma once

#include "cli/commands/Command.hpp"

namespace uniforge::cli {

// `uniforge_cli fea`: runs the Task C convergence study on the brief's rod (N = 10, 20, 40, 80)
// and writes the evidence (spec §7.3):
//   csv/fea_N{N}_nodes.csv, csv/fea_N{N}_midpoints.csv, csv/convergence.csv (+ .meta.json each)
//   plots/T_vs_x.svg, plots/error_vs_h_loglog.svg
// Prints the convergence table and PASS/FAIL for the verification checks. CSVs and SVGs are
// byte-identical on every run; only the sidecars carry the run time.
int runFea(const CommandContext& ctx);

} // namespace uniforge::cli
