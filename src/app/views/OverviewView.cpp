#include "app/views/OverviewView.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#include <imgui.h>

#include "app/services/DemoJob.hpp"
#include "app/services/JobRunner.hpp"
#include "app/services/ResultStore.hpp"
#include "app/ui/Theme.hpp"
#include "app/ui/widgets/Widgets.hpp"
#include "core/ml/GradientCheck.hpp"
#include "core/perf/Benchmark.hpp"

namespace uniforge::views {

namespace {

constexpr const char* kDemoJobName = "demo.steps";

struct TaskRow {
    const char* tag;
    const char* name;
    const char* summary;
    const char* arrives;
    ViewId view;
};

constexpr TaskRow kTasks[] = {
    {"A", "Workbench shell", "One UI, background jobs with progress and cancel, one-click launch",
     "Phase 1", ViewId::About},
    {"B", "Geometry", "ASCII STL viewer and nodal-temperature CSV point cloud", "Phase 4",
     ViewId::Geometry},
    {"C", "Thermal Analysis", "1D steady FEA, N = 10/20/40/80, convergence vs exact", "Phase 4",
     ViewId::Thermal},
    {"D", "Performance", "Serial vs parallel batch solves, 5-rep median benchmark", "Phase 5",
     ViewId::Performance},
    {"E", "Hardware", "Simulated MCU, FPGA and JTAG adapters with fault recovery", "Phase 6",
     ViewId::Hardware},
    {"F", "Backprop", "One-neuron gradients checked by central differences", "Phase 7",
     ViewId::Backprop},
};

// Live status: A-D from this session's results ("Pending" until there are some), E the
// simulated-device notice, F the brief's gradient check computed on the spot.
void drawTaskStatus(const AppContext& ctx, const TaskRow& row) {
    switch (row.view) {
    case ViewId::About:
        if (ctx.runtime.launchMeasured) {
            ui::statusLabel(ui::Status::Success, "Running",
                            ctx.runtime.render.software() ? "software rendering"
                                                          : "hardware rendering");
            return;
        }
        break;
    case ViewId::Geometry: {
        const auto mesh = ctx.results.geometryMesh.get();
        const auto points = ctx.results.geometryPoints.get();
        if (mesh || points) {
            const std::string detail =
                std::string(mesh ? "STL loaded" : "no STL") + " · " +
                (points ? std::to_string(points->source.nodes.size()) + " nodes" : "no CSV");
            ui::statusLabel(ui::Status::Success, "Loaded", detail.c_str());
            return;
        }
        ui::statusLabel(ui::Status::Pending, "Pending", "load an STL / CSV");
        return;
    }
    case ViewId::Thermal: {
        const auto study = ctx.results.thermal.get();
        if (study && !study->rows.empty()) {
            bool pass = true;
            for (const auto& r : study->rows) {
                pass = pass && std::fabs(r.maxMidErr - r.expected) <= 1e-9;
            }
            const std::string detail =
                "midpoint error = qh²/8k for " + std::to_string(study->rows.size()) + " meshes";
            ui::statusLabel(pass ? ui::Status::Success : ui::Status::Error, pass ? "Pass" : "Fail",
                            detail.c_str());
            return;
        }
        ui::statusLabel(ui::Status::Pending, "Pending", "run the study");
        return;
    }
    case ViewId::Performance: {
        const auto bench = ctx.results.benchmark.get();
        if (bench && !bench->rows.empty()) {
            const perf::BenchRow& last = bench->rows.back();
            const bool pass = bench->agreementPass && bench->orderPreserved;
            char detail[96];
            std::snprintf(detail, sizeof detail, "speedup %.2f at p = %d, serial = parallel",
                          last.speedup, last.workers);
            ui::statusLabel(pass ? ui::Status::Success : ui::Status::Error, pass ? "Pass" : "Fail",
                            detail);
            return;
        }
        ui::statusLabel(ui::Status::Pending, "Pending", "run a benchmark");
        return;
    }
    case ViewId::Hardware:
        ui::statusLabel(ui::Status::Simulated, "Simulated", "3 mock devices, no hardware needed");
        return;
    case ViewId::Backprop: {
        // The brief's check is a few microseconds of work, so it runs live here.
        const ml::BackpropProblem brief;
        const ml::GradCheck check = ml::centralDiff(brief.init, brief.x, brief.y);
        ui::statusLabel(check.pass ? ui::Status::Success : ui::Status::Error,
                        check.pass ? "Pass" : "Fail", "analytic = central differences, 4/4");
        return;
    }
    default:
        break;
    }
    ui::statusLabel(ui::Status::Pending, "Pending", row.arrives);
}

} // namespace

void OverviewView::draw(AppContext& ctx) {
    drawDemoJob(ctx);
    ImGui::Dummy(ImVec2(0.0f, ui::theme().size.space16));
    drawTasks(ctx);
}

void OverviewView::drawTasks(AppContext& ctx) {
    const ui::Theme& t = ui::theme();
    ui::beginCard("##tasks");
    ui::cardTitle("Tasks");
    ui::secondaryText("Each task gets its own view with verification badges as it is built.");
    ImGui::Dummy(ImVec2(0.0f, t.size.space4));

    const ImGuiTableFlags flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##tasktable", 5, flags)) {
        ImGui::TableSetupColumn("Task", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("View", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("What it does", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("##open", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for (const TaskRow& row : kTasks) {
            ImGui::PushID(row.tag);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ui::chip(row.tag, t.color.accent);
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(row.name);
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ui::secondaryText("%s", row.summary);
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            drawTaskStatus(ctx, row);
            ImGui::TableNextColumn();
            if (ui::secondaryButton("Open view →")) {
                ctx.navigateTo = row.view;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ui::endCard();
}

void OverviewView::startDemo(AppContext& ctx) {
    const DemoJobConfig config{steps_, std::chrono::milliseconds(stepMs_)};
    const std::uint64_t run = ++runsStarted_;
    ctx.jobs.submitPublishing(kDemoJobName, ctx.results.demo, [config, run](JobContext& job) {
        return runDemoJob(job, config, run);
    });
}

void OverviewView::drawDemoJob(AppContext& ctx) {
    const ui::Theme& t = ui::theme();
    const JobSnapshot job = ctx.jobs.snapshot();
    const bool running = job.state == JobState::Running;
    const bool demoIsLast = job.name == kDemoJobName;

    ui::beginCard("##demojob");
    ui::cardTitle("Background job demo");
    ui::secondaryText("Sleeps in steps on the job thread to check progress reporting, "
                      "cancellation and result publishing. The UI keeps rendering while it runs; "
                      "a cancelled run never replaces the last completed result.");
    ImGui::Dummy(ImVec2(0.0f, t.size.space4));

    // Parameters
    ImGui::BeginDisabled(running);
    ImGui::SetNextItemWidth(260.0f * t.dpiScale);
    ImGui::SliderInt("Steps [count]", &steps_, 1, 100);
    ImGui::SetNextItemWidth(260.0f * t.dpiScale);
    ImGui::SliderInt("Step duration [ms]", &stepMs_, 10, 1000);
    ImGui::EndDisabled();
    if (running && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Parameters are locked while a job is running");
    }
    ui::secondaryText("Expected duration: %.1f s", steps_ * stepMs_ / 1000.0);
    ImGui::Dummy(ImVec2(0.0f, t.size.space4));

    // Run bar: Run <-> Cancel, progress, stage, elapsed.
    if (running) {
        const bool cancelling = job.cancelRequested;
        if (ui::secondaryButton(cancelling ? "Cancelling…" : "Cancel  Esc", !cancelling,
                                "Cancel already requested") ||
            (!cancelling && ImGui::Shortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteGlobal))) {
            ctx.jobs.cancel();
        }
    } else if (ui::primaryButton("Run demo job  Ctrl+Enter") ||
               ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Enter, ImGuiInputFlags_RouteGlobal)) {
        startDemo(ctx);
    }
    ImGui::SameLine(0.0f, t.size.space12);

    const float fraction = demoIsLast ? static_cast<float>(job.fraction) : 0.0f;
    char overlay[32];
    std::snprintf(overlay, sizeof overlay, "%.0f %%", fraction * 100.0f);
    ImGui::ProgressBar(fraction, ImVec2(280.0f * t.dpiScale, 0.0f), overlay);
    if (demoIsLast && (running || job.state != JobState::Idle)) {
        ImGui::SameLine(0.0f, t.size.space12);
        ImGui::AlignTextToFramePadding();
        ui::secondaryText("%s · %.2f s", job.stage.c_str(), job.elapsedSeconds);
    }

    // Outcome of the last demo run.
    if (demoIsLast) {
        switch (job.state) {
        case JobState::Succeeded:
            ui::statusLabel(ui::Status::Success, "Done", job.message.c_str());
            break;
        case JobState::Cancelled:
            ui::statusLabel(ui::Status::Warning, "Cancelled", "previous result kept");
            break;
        case JobState::Failed:
            ui::statusLabel(ui::Status::Error, "Failed", job.message.c_str());
            break;
        case JobState::Running:
        case JobState::Idle:
            break;
        }
    }

    // The published result: only ever replaced by a run that finished.
    ImGui::Dummy(ImVec2(0.0f, t.size.space4));
    const auto result = ctx.results.demo.get();
    if (result) {
        ui::secondaryText("Last completed result: run #%llu · %d steps · %.2f s%s",
                          static_cast<unsigned long long>(result->run), result->steps,
                          result->seconds, running ? "  (kept while the next run works)" : "");
    } else {
        ui::secondaryText("No completed result yet.");
    }
    ui::endCard();
}

} // namespace uniforge::views
