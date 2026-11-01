#pragma once

#include "app/AppContext.hpp"

namespace uniforge::ui {

struct NavItem {
    ViewId id;
    const char* label;    // "Thermal"
    const char* shortTag; // "C" (shown right-aligned; the compact rail shows only this)
    const char* hint;     // tooltip, e.g. "Task C · 1D thermal FEA (Phase 2)"
};

// Left navigation rail (spec §8.4/8.6). Ctrl+1 … Ctrl+7 switch views.
// Returns the view to show this frame.
ViewId drawNavRail(ViewId current, bool compact, float width, float height);

} // namespace uniforge::ui
