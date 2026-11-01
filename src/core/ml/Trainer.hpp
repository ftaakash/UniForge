#pragma once

#include <vector>

#include "core/ml/TinyNet.hpp"

namespace uniforge::ml {

// Full-batch gradient descent on the single sample: `steps` simultaneous updates
// p ← sgdStep(p, backward(p, x, y), lr). Returns steps + 1 losses: element 0 is J at the initial
// parameters, element k is J after k updates. `finalParams` (optional) receives p after the
// last update. steps < 0 is treated as 0.
[[nodiscard]] std::vector<double> train(const Params& init, double x, double y, double lr,
                                        int steps, Params* finalParams = nullptr);

} // namespace uniforge::ml
