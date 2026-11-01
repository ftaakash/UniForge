#pragma once

#include <functional>
#include <string_view>

namespace uniforge {

// Progress callback passed into long-running core functions.
// fraction is in [0, 1]; stage is a short human-readable step ("Rep 3/5 · p = 2").
// Implementations must be cheap and thread-safe: they are called from worker threads.
using ProgressFn = std::function<void(double fraction, std::string_view stage)>;

} // namespace uniforge
