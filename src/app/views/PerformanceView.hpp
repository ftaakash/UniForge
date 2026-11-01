#pragma once

#include <cstdint>
#include <string>

#include "app/views/View.hpp"

namespace uniforge::views {

// Task D (spec §8.6): serial vs parallel batch benchmark. Left: workload parameters, presets
// and the run bar; right: verification badges, Timing / Speedup & efficiency / System tabs and
// the per-p table. The benchmark runs on the JobRunner thread through perf::benchmark and is
// published to ResultStore::benchmark only when it completes.
class PerformanceView final : public View {
public:
    [[nodiscard]] const char* title() const override { return "Performance"; }
    [[nodiscard]] const char* tag() const override { return "Task D"; }
    [[nodiscard]] const char* purpose() const override {
        return "Serial vs CPU-parallel batch of independent rod solves with a reproducible "
               "benchmark (1 warm-up + 5 timed repetitions per worker count).";
    }
    void draw(AppContext& ctx) override;

private:
    void drawParameters(AppContext& ctx);
    void drawRunBar(AppContext& ctx);
    void drawResults(AppContext& ctx);
    void start(AppContext& ctx);
    void exportReport(AppContext& ctx);

    std::uint64_t seed_ = 20261101;
    int cases_ = 500; // Quick preset by default: about a second for the whole protocol
    int elements_ = 1000;
    bool useP1_ = true, useP2_ = true, useP4_ = true;
    std::string exportMessage_;
    bool exportOk_ = false;
};

} // namespace uniforge::views
