#pragma once

#include <cstdint>

#include "app/views/View.hpp"

namespace uniforge::views {

// Home view (spec D13). In Phase 1 it lists the task views and hosts a demo background job
// that exercises JobRunner progress, cancellation and "previous result kept" end to end.
// The live task cards and "Run all self-checks" arrive as the tasks are built.
class OverviewView final : public View {
public:
    [[nodiscard]] const char* title() const override { return "Overview"; }
    [[nodiscard]] const char* tag() const override { return "Task A"; }
    [[nodiscard]] const char* purpose() const override {
        return "One workbench for geometry import, thermal FEA, multicore benchmarking, "
               "embedded adapters and backpropagation.";
    }
    void draw(AppContext& ctx) override;

private:
    void drawTasks(AppContext& ctx);
    void drawDemoJob(AppContext& ctx);
    void startDemo(AppContext& ctx);

    int steps_ = 20;
    int stepMs_ = 100;
    std::uint64_t runsStarted_ = 0;
};

} // namespace uniforge::views
