#pragma once

#include <chrono>
#include <cstdint>
#include <memory>

namespace uniforge {

class JobContext;

// Result of the Overview demo job: proves progress reporting, cancellation and atomic publish
// end to end before any real solver exists.
struct DemoJobResult {
    std::uint64_t run = 0; // 1-based count of completed demo runs
    int steps = 0;
    double seconds = 0.0;
};

struct DemoJobConfig {
    int steps = 20;
    std::chrono::milliseconds stepDuration{100};
};

// Sleeps `steps` times, reporting progress after each step and polling for cancellation at
// least every 20 ms (so cancel latency stays well under the 200 ms budget, spec N4).
// Returns nullptr when cancelled; `run` is the number to store in the result.
[[nodiscard]] std::shared_ptr<const DemoJobResult>
runDemoJob(JobContext& ctx, const DemoJobConfig& config, std::uint64_t run);

} // namespace uniforge
