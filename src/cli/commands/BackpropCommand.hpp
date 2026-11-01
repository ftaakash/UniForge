#pragma once

#include "cli/commands/Command.hpp"

namespace uniforge::cli {

// `uniforge_cli backprop`: runs the Task F one-neuron experiment with the brief's values and
// writes the evidence (spec §7.6):
//   csv/backprop_gradients.csv  param, analytic, numeric (central FD, eps = 1e-6), abs_diff, pass
//   csv/backprop_update.csv     param, before, gradient, after one simultaneous step at lr
//   csv/loss_curve.csv          step, loss for steps 0..100 (+ .meta.json each)
//   plots/loss_curve.svg, plots/loss_curve_logy.svg
// Prints the forward pass, the gradient table, the update and PASS/FAIL for the checks. CSVs and
// SVGs are byte-identical on every run; only the sidecars carry the run time.
int runBackprop(const CommandContext& ctx);

} // namespace uniforge::cli
