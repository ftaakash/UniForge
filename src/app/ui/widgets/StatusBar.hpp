#pragma once

#include "app/AppContext.hpp"

namespace uniforge::ui {

// Bottom strip (spec §8.6): job state · render backend · logical CPUs · launch-to-ready.
void drawStatusBar(const AppContext& ctx, float width, float height);

} // namespace uniforge::ui
