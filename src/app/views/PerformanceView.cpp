#include "app/views/PerformanceView.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

#include <imgui.h>
#include <implot.h>

#include "app/BuildInfo.hpp"
#include "app/platform/Text.hpp"
#include "app/services/JobRunner.hpp"
#include "app/services/LogSink.hpp"
#include "app/services/ResultStore.hpp"
#include "app/ui/Theme.hpp"
#include "app/ui/widgets/Widgets.hpp"
#include "core/common/MetaJson.hpp"
#include "core/perf/Benchmark.hpp"
#include "core/perf/BenchmarkReportIO.hpp"

namespace uniforge::views {

namespace {

constexpr const char* kJobName = "perf.bench";

// One verification badge: glyph + word + detail, never colour-only (spec §8.1).
void badge(bool known, bool pass, const char* word, const char* detail) {
    const ui::Status status =
        !known ? ui::Status::Pending : (pass ? ui::Status::Success : ui::Status::Error);
    ui::statusLabel(status, word, detail);
}

std::vector<double> column(const perf::BenchmarkReport& r, double perf::BenchRow::*field) {
    std::vector<double> v;
    for (const perf::BenchRow& row : r.rows) {
        v.push_back(row.*field);
    }
    return v;
}

void drawTimingPlot(const perf::BenchmarkReport& r) {
    const ui::Theme& t = ui::theme();
    const std::vector<double> p = [&] {
        std::vector<double> v;
        for (const perf::BenchRow& row : r.rows) {
            v.push_back(static_cast<double>(row.workers));
        }
        return v;
    }();
    const std::vector<double> median = column(r, &perf::BenchRow::median);
    std::vector<double> neg, pos, ideal;
    for (const perf::BenchRow& row : r.rows) {
        neg.push_back(row.median - row.min);
        pos.push_back(row.max - row.median);
        ideal.push_back(r.rows.front().median / row.workers);
    }
    const int n = static_cast<int>(p.size());
    if (ImPlot::BeginPlot("##timing", ImVec2(-1.0f, 300.0f * t.dpiScale),
                          ImPlotFlags_NoMouseText)) {
        ImPlot::SetupAxes("workers p", "compute time per batch [s]", ImPlotAxisFlags_AutoFit,
                          ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisTicks(ImAxis_X1, p.data(), n);
        ImPlotSpec bars;
        bars.FillColor = t.color.accent;
        bars.LineColor = t.color.accent;
        ImPlot::PlotBars("median", p.data(), median.data(), n, 0.5, bars);
        ImPlotSpec whiskers;
        whiskers.LineColor = t.color.textPrimary;
        whiskers.LineWeight = 1.5f;
        ImPlot::PlotErrorBars("min-max", p.data(), median.data(), neg.data(), pos.data(), n,
                              whiskers);
        ImPlotSpec idealLine;
        idealLine.LineColor = t.color.textSecondary;
        idealLine.Marker = ImPlotMarker_Circle;
        ImPlot::PlotLine("ideal T1/p", p.data(), ideal.data(), n, idealLine);
        ImPlot::EndPlot();
    }
}

void drawSpeedupPlots(const perf::BenchmarkReport& r) {
    const ui::Theme& t = ui::theme();
    std::vector<double> p, speedup, efficiency;
    for (const perf::BenchRow& row : r.rows) {
        p.push_back(static_cast<double>(row.workers));
        speedup.push_back(row.speedup);
        efficiency.push_back(row.efficiency * 100.0);
    }
    const int n = static_cast<int>(p.size());
    const float width = (ImGui::GetContentRegionAvail().x - t.size.space12) * 0.5f;
    const ImVec2 size(width, 280.0f * t.dpiScale);
    if (ImPlot::BeginPlot("Speedup T1/Tp", size, ImPlotFlags_NoMouseText)) {
        ImPlot::SetupAxes("workers p", "speedup [x]", ImPlotAxisFlags_AutoFit,
                          ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisTicks(ImAxis_X1, p.data(), n);
        ImPlotSpec idealLine;
        idealLine.LineColor = t.color.textSecondary;
        ImPlot::PlotLine("ideal = p", p.data(), p.data(), n, idealLine);
        ImPlotSpec measured;
        measured.LineColor = t.color.accent;
        measured.LineWeight = 2.0f;
        measured.Marker = ImPlotMarker_Circle;
        ImPlot::PlotLine("measured", p.data(), speedup.data(), n, measured);
        ImPlot::EndPlot();
    }
    ImGui::SameLine(0.0f, t.size.space12);
    if (ImPlot::BeginPlot("Efficiency speedup/p", size, ImPlotFlags_NoMouseText)) {
        ImPlot::SetupAxes("workers p", "efficiency [%]", ImPlotAxisFlags_AutoFit, 0);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 110.0, ImPlotCond_Always);
        ImPlot::SetupAxisTicks(ImAxis_X1, p.data(), n);
        ImPlotSpec bars;
        bars.FillColor = t.color.accent;
        bars.LineColor = t.color.accent;
        ImPlot::PlotBars("efficiency", p.data(), efficiency.data(), n, 0.5, bars);
        ImPlot::EndPlot();
    }
}

void drawSystem(const perf::BenchmarkReport& r) {
    const perf::SystemInfo& s = r.sys;
    char buf[256];
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH;
    if (ImGui::BeginTable("##system", 2, flags)) {
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        const auto row = [](const char* k, const std::string& v) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ui::secondaryText("%s", k);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", v.empty() ? "unknown" : v.c_str());
        };
        row("CPU", s.cpu);
        std::snprintf(buf, sizeof buf, "%d physical, %d logical%s%s", s.physicalCores,
                      s.logicalCpus, s.coreTypes.empty() ? "" : "; ", s.coreTypes.c_str());
        row("Cores", buf);
        std::snprintf(buf, sizeof buf, "%.1f GB",
                      static_cast<double>(s.ramBytes) / (1024.0 * 1024.0 * 1024.0));
        row("RAM", buf);
        row("OS", s.os);
        row("Compiler", s.compiler + " (" + s.buildType + ")");
        row("Flags", s.flags);
        std::snprintf(buf, sizeof buf, "%d cases x %d elements, seed %llu, %.1f MB per result set",
                      r.cfg.cases, r.cfg.elements, static_cast<unsigned long long>(r.cfg.seed),
                      r.resultSetBytes / 1e6);
        row("Workload", buf);
        std::snprintf(buf, sizeof buf, "%d warm-up + %d timed repetitions per p; median",
                      perf::kWarmupReps, perf::kTimedReps);
        row("Protocol", buf);
        ImGui::EndTable();
    }
    for (const std::string& note : r.notes) {
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::PushTextWrapPos(0.0f);
        ui::secondaryText("%s", note.c_str());
        ImGui::PopTextWrapPos();
    }
}

void drawTable(const perf::BenchmarkReport& r) {
    const ImGuiTableFlags flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##benchtable", 6 + perf::kTimedReps, flags)) {
        return;
    }
    ImGui::TableSetupColumn("p");
    ImGui::TableSetupColumn("warm-up [s]");
    char label[16];
    for (int i = 1; i <= perf::kTimedReps; ++i) {
        std::snprintf(label, sizeof label, "rep %d [s]", i);
        ImGui::TableSetupColumn(label);
    }
    ImGui::TableSetupColumn("median [s]");
    ImGui::TableSetupColumn("min–max [s]");
    ImGui::TableSetupColumn("speedup");
    ImGui::TableSetupColumn("efficiency");
    ImGui::TableHeadersRow();
    for (const perf::BenchRow& row : r.rows) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%d", row.workers);
        ImGui::TableNextColumn();
        ui::secondaryText("%.4f", row.warmupSec);
        for (const double s : row.repsSec) {
            ImGui::TableNextColumn();
            ImGui::Text("%.4f", s);
        }
        ImGui::TableNextColumn();
        ImGui::Text("%.4f", row.median);
        ImGui::TableNextColumn();
        ImGui::Text("%.4f–%.4f", row.min, row.max);
        ImGui::TableNextColumn();
        if (r.speedupAssessable) {
            ImGui::Text("%.2f×", row.speedup);
        } else {
            ui::secondaryText("n/a");
        }
        ImGui::TableNextColumn();
        if (r.speedupAssessable) {
            ImGui::Text("%.0f %%", row.efficiency * 100.0);
        } else {
            ui::secondaryText("n/a");
        }
    }
    ImGui::EndTable();
}

} // namespace

void PerformanceView::draw(AppContext& ctx) {
    const ui::Theme& t = ui::theme();
    const float leftWidth = 330.0f * t.dpiScale;
    ImGui::BeginGroup();
    ui::beginCard("##perfparams", leftWidth);
    drawParameters(ctx);
    ui::endCard();
    ImGui::Dummy(ImVec2(0.0f, t.size.space12));
    ui::beginCard("##perfrun", leftWidth);
    drawRunBar(ctx);
    ui::endCard();
    ImGui::EndGroup();
    ImGui::SameLine(0.0f, t.size.space16);
    ImGui::BeginGroup();
    ui::beginCard("##perfresults");
    drawResults(ctx);
    ui::endCard();
    ImGui::EndGroup();
}

void PerformanceView::drawParameters(AppContext& ctx) {
    const ui::Theme& t = ui::theme();
    const bool running = ctx.jobs.busy();
    const int logical = static_cast<int>(std::max(1U, ctx.runtime.logicalCpus));
    ui::cardTitle("Workload");
    ImGui::BeginDisabled(running);
    if (ui::secondaryButton("Quick  500 × 1000")) {
        const perf::BenchmarkConfig q = perf::quickPreset();
        seed_ = q.seed;
        cases_ = q.cases;
        elements_ = q.elements;
    }
    ImGui::SameLine(0.0f, t.size.space8);
    if (ui::secondaryButton("Report  10000 × 8000")) {
        const perf::BenchmarkConfig r = perf::reportPreset();
        seed_ = r.seed;
        cases_ = r.cases;
        elements_ = r.elements;
    }
    const float fieldWidth = 150.0f * t.dpiScale;
    ImGui::SetNextItemWidth(fieldWidth);
    ImGui::InputScalar("Seed", ImGuiDataType_U64, &seed_);
    ImGui::SetNextItemWidth(fieldWidth);
    ImGui::InputInt("Cases [count]", &cases_, 100, 1000);
    cases_ = std::clamp(cases_, 1, 100000);
    ImGui::SetNextItemWidth(fieldWidth);
    ImGui::InputInt("Elements per case", &elements_, 100, 1000);
    elements_ = std::clamp(elements_, 1, 100000);

    ImGui::Dummy(ImVec2(0.0f, t.size.space4));
    ImGui::TextUnformatted("Workers p");
    ImGui::Checkbox("1 (serial)", &useP1_);
    useP1_ = true; // T1 baseline, always run
    ImGui::SameLine();
    ImGui::BeginDisabled(logical < 2);
    ImGui::Checkbox("2", &useP2_);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(logical < 4);
    ImGui::Checkbox("4", &useP4_);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::PushTextWrapPos(0.0f);
    ui::secondaryText("Capped at %d logical CPUs on this machine. p = 1 is the serial baseline.",
                      logical);
    const double mb = 2.0 * cases_ * (elements_ + 1.0) * 8.0 / 1e6;
    ui::secondaryText("k ~ U[5, 50] W/(m·K), q ~ U[1e3, 5e4] W/m³. Memory for results: %.0f MB.",
                      mb);
    ImGui::PopTextWrapPos();
}

void PerformanceView::start(AppContext& ctx) {
    perf::BenchmarkConfig cfg;
    cfg.seed = seed_;
    cfg.cases = cases_;
    cfg.elements = elements_;
    cfg.workers = {1};
    if (useP2_) {
        cfg.workers.push_back(2);
    }
    if (useP4_) {
        cfg.workers.push_back(4);
    }
    exportMessage_.clear();
    LogSink* log = &ctx.log;
    ctx.jobs.submitPublishing(
        kJobName, ctx.results.benchmark,
        [cfg, log](JobContext& job) -> std::shared_ptr<const perf::BenchmarkReport> {
            auto r = perf::benchmark(cfg, job.progressFn(), &job.cancelToken());
            if (!r) {
                if (r.error().code == ErrorCode::Cancelled) {
                    return nullptr; // nothing published; the previous result stays
                }
                throw JobError(r.error().message + " " + r.error().hint);
            }
            char line[200];
            const perf::BenchmarkReport& rep = r.value();
            std::snprintf(line, sizeof line,
                          "bench %d x %d: T1 %.4f s, T%d %.4f s, speedup %.2f, max|dT| %.3g C",
                          cfg.cases, cfg.elements, rep.rows.front().median,
                          rep.rows.back().workers, rep.rows.back().median,
                          rep.rows.back().speedup, rep.maxAbsDiff_C);
            log->info(line);
            return std::make_shared<const perf::BenchmarkReport>(std::move(r).value());
        });
}

void PerformanceView::exportReport(AppContext& ctx) {
    const auto report = ctx.results.benchmark.get();
    if (!report) {
        return;
    }
    perf::ReportProvenance prov;
    prov.tool = "UniForge Performance view";
    prov.toolVersion = build::kVersion;
    prov.gitCommit = build::kGitCommit;
    const auto now = std::chrono::system_clock::now();
    prov.generatedUtc = formatUtcIso8601(now);
    prov.command = "uniforge_cli bench --seed " + std::to_string(report->cfg.seed) +
                   " --cases " + std::to_string(report->cfg.cases) + " --elements " +
                   std::to_string(report->cfg.elements);
    const auto dir = ctx.runtime.paths.stateDir / L"results" / L"benchmarks";
    const std::string stem = "bench_ui_" + prov.generatedUtc.substr(0, 10);
    const auto saved = perf::saveBenchmarkReport(*report, prov, dir, stem);
    exportOk_ = saved.ok();
    exportMessage_ = exportOk_ ? "Saved " + platform::pathToUtf8(dir) + "\\" + stem +
                                     ".{json,csv,md}"
                               : saved.error().message;
    if (exportOk_) {
        ctx.log.info("exported " + stem + ".{json,csv,md}");
    } else {
        ctx.log.error("benchmark export failed: " + exportMessage_);
    }
}

void PerformanceView::drawRunBar(AppContext& ctx) {
    const ui::Theme& t = ui::theme();
    const JobSnapshot job = ctx.jobs.snapshot();
    const bool running = job.state == JobState::Running;
    const bool mine = job.name == kJobName;
    ui::cardTitle("Run");
    if (running) {
        const bool cancelling = job.cancelRequested;
        if (ui::secondaryButton(cancelling ? "Cancelling…" : "Cancel  Esc", !cancelling,
                                "Cancel already requested") ||
            (!cancelling && ImGui::Shortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteGlobal))) {
            ctx.jobs.cancel();
        }
    } else if (ui::primaryButton("Run benchmark  Ctrl+Enter") ||
               ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Enter, ImGuiInputFlags_RouteGlobal)) {
        start(ctx);
    }
    const float fraction = mine ? static_cast<float>(job.fraction) : 0.0f;
    char overlay[32];
    std::snprintf(overlay, sizeof overlay, "%.0f %%", fraction * 100.0f);
    ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay);
    if (mine && job.state != JobState::Idle) {
        ui::secondaryText("%s · %.1f s", job.stage.c_str(), job.elapsedSeconds);
        switch (job.state) {
        case JobState::Succeeded:
            ui::statusLabel(ui::Status::Success, "Done", "results below");
            break;
        case JobState::Cancelled:
            ui::statusLabel(ui::Status::Warning, "Cancelled", "previous result kept");
            break;
        case JobState::Failed:
            ImGui::PushTextWrapPos(0.0f);
            ui::statusLabel(ui::Status::Error, "Failed", job.message.c_str());
            ImGui::PopTextWrapPos();
            break;
        case JobState::Running:
        case JobState::Idle:
            break;
        }
    }
    ImGui::Dummy(ImVec2(0.0f, t.size.space4));
    const bool haveResult = ctx.results.benchmark.get() != nullptr;
    if (ui::secondaryButton("Export report", haveResult && !running,
                            "Run a benchmark first")) {
        exportReport(ctx);
    }
    if (!exportMessage_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ui::statusLabel(exportOk_ ? ui::Status::Success : ui::Status::Error,
                        exportOk_ ? "Exported" : "Export failed", exportMessage_.c_str());
        ImGui::PopTextWrapPos();
    }
}

void PerformanceView::drawResults(AppContext& ctx) {
    const ui::Theme& t = ui::theme();
    const auto report = ctx.results.benchmark.get();
    const bool known = report != nullptr;

    // Badges (spec §8.6): agreement, order preserved, protocol.
    char detail[96];
    std::snprintf(detail, sizeof detail, "max |ΔT| = %.3g °C ≤ 1e-8",
                  known ? report->maxAbsDiff_C : 0.0);
    badge(known, known && report->agreementPass, "Agreement", known ? detail : "not run yet");
    ImGui::SameLine(0.0f, t.size.space24);
    badge(known, known && report->orderPreserved, "Order preserved", "out[i] ↔ case i");
    ImGui::SameLine(0.0f, t.size.space24);
    badge(known, known, "5 reps + warm-up", "median per p");

    if (ctx.runtime.logicalCpus <= 1 || (known && !report->speedupAssessable)) {
        ImGui::Dummy(ImVec2(0.0f, t.size.space4));
        ImGui::PushTextWrapPos(0.0f);
        ui::statusLabel(ui::Status::Warning, "Single logical CPU",
                        "p = 2 runs only as a correctness demonstration; speedup is not "
                        "assessable on this machine.");
        ImGui::PopTextWrapPos();
    }
    ImGui::Dummy(ImVec2(0.0f, t.size.space8));

    if (!known) {
        ui::emptyState("No benchmark yet",
                       "Choose a preset and press Run. Quick takes about a second; Report is the "
                       "workload behind the committed results (about a minute).");
        return;
    }
    if (ImGui::BeginTabBar("##perftabs")) {
        if (ImGui::BeginTabItem("Timing")) {
            drawTimingPlot(*report);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Speedup & efficiency")) {
            if (report->speedupAssessable) {
                drawSpeedupPlots(*report);
            } else {
                ui::emptyState("Speedup not assessable", "This machine has one logical CPU.");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("System")) {
            drawSystem(*report);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::Dummy(ImVec2(0.0f, t.size.space8));
    drawTable(*report);
}

} // namespace uniforge::views
