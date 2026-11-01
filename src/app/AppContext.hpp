#pragma once

#include <optional>
#include <string>

#include "app/platform/D3D11Context.hpp"
#include "app/platform/LaunchTiming.hpp"
#include "app/platform/Paths.hpp"

namespace uniforge {

class JobRunner;
class LogSink;
struct ResultStore;

// Navigation targets, in NavRail order.
enum class ViewId { Overview, Geometry, Thermal, Performance, Hardware, Backprop, About };
inline constexpr int kViewCount = 7;

// Facts about this run that views display (About, StatusBar). Written by App on the UI thread.
struct RuntimeInfo {
    platform::AppPaths paths;
    platform::RenderInfo render;
    platform::LaunchTiming launch;
    bool launchMeasured = false; // set at the first successful Present
    unsigned logicalCpus = 0;
};

// Everything a view may use. Views run on the UI thread only.
struct AppContext {
    LogSink& log;
    JobRunner& jobs;
    ResultStore& results;
    const RuntimeInfo& runtime;
    std::optional<ViewId> navigateTo; // set by a view to switch views after this frame
};

} // namespace uniforge
