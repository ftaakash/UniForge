#include "app/services/DemoJob.hpp"

#include <algorithm>
#include <string>
#include <thread>

#include "app/services/JobRunner.hpp"

namespace uniforge {

std::shared_ptr<const DemoJobResult> runDemoJob(JobContext& ctx, const DemoJobConfig& config,
                                                std::uint64_t run) {
    using clock = std::chrono::steady_clock;
    constexpr auto kPollInterval = std::chrono::milliseconds(20);
    const int steps = std::max(1, config.steps);
    const auto start = clock::now();

    for (int i = 0; i < steps; ++i) {
        ctx.report(static_cast<double>(i) / steps,
                   "Step " + std::to_string(i + 1) + "/" + std::to_string(steps));
        const auto stepEnd = clock::now() + config.stepDuration;
        while (clock::now() < stepEnd) {
            if (ctx.cancelled()) {
                return nullptr;
            }
            std::this_thread::sleep_for(
                std::min<clock::duration>(kPollInterval, stepEnd - clock::now()));
        }
    }
    ctx.report(1.0, "Done");

    auto result = std::make_shared<DemoJobResult>();
    result->run = run;
    result->steps = steps;
    result->seconds = std::chrono::duration<double>(clock::now() - start).count();
    return result;
}

} // namespace uniforge
