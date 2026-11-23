#pragma once

#include <cstddef>

#include "core/viz/Image.hpp"

namespace uniforge::viz {

// Inferno colormap (figure tokens: temperature fields), t clamped to [0, 1]: dark → bright.
[[nodiscard]] Rgb inferno(double t);

// Value → colour on a fixed range [lo, hi] (all views of one case share one range).
[[nodiscard]] Rgb inferno(double value, double lo, double hi);

// Categorical colours for labels (AMG aggregates, materials): neighbouring indices differ
// clearly; the palette repeats after a few dozen entries.
[[nodiscard]] Rgb categorical(std::size_t index);

} // namespace uniforge::viz
