#include "app/views/ThermalView.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include <imgui.h>
#include <implot.h>

#include "app/platform/FileDialog.hpp"
#include "app/platform/ScreenCapture.hpp"
#include "app/platform/Text.hpp"
#include "app/services/JobRunner.hpp"
#include "app/services/LogSink.hpp"
#include "app/services/ResultStore.hpp"
#include "app/ui/Format.hpp"
#include "app/ui/Theme.hpp"
#include "app/ui/ThemeViz.hpp"
#include "app/ui/widgets/Forms.hpp"
#include "app/ui/widgets/Widgets.hpp"

namespace uniforge::views {

namespace {

constexpr const char* kJob = "thermal.study";
constexpr int kMeshSizes[4] = {10, 20, 40, 80};
constexpr ImPlotMarker kMarkers[4] = {ImPlotMarker_Circle, ImPlotMarker_Square,
                                      ImPlotMarker_Diamond, ImPlotMarker_Up};

// Tolerances shown next to every verdict (spec §2.2 N5 and §7.3).
constexpr double kMidTol = 1e-9;    // °C, |measured − qh²/8k|
constexpr double kOrderTol = 0.01;  // |order − 2|
constexpr double kEnergyTol = 1e-9; // W
constexpr double kNodalTol = 1e-9;  // °C

const thermal::RodProblem kBrief{};

ImPlotMarker markerFor(int n) {
    const int i = ui::seriesIndexForMesh(n);
    return kMarkers[i < 4 ? i : 0];
}

// Exact curve with gaps every few samples: drawn dashed (NaN breaks an ImPlot line).
void dashed(const std::vector<double>& x, const std::vector<double>& y, std::vector<double>& dx,
            std::vector<double>& dy) {
    dx.clear();
    dy.clear();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (std::size_t i = 0; i < x.size(); ++i) {
        const bool gap = (i % 6) == 5;
        dx.push_back(gap ? nan : x[i]);
        dy.push_back(gap ? nan : y[i]);
    }
}

ImPlotSpec referenceSpec() {
    ImPlotSpec s;
    s.LineColor = ui::referenceColor();
    s.LineWeight = 1.5f * ui::theme().dpiScale;
    return s;
}

} // namespace

void ThermalView::draw(AppContext& ctx) {
    const ui::Theme& t = ui::theme();
    if (pngPending_) {
        platform::requestCapture({*pngPending_, static_cast<int>(plotRect_.x),
                                  static_cast<int>(plotRect_.y), static_cast<int>(plotRect_.w),
                                  static_cast<int>(plotRect_.h)});
        pngPending_.reset();
    }
    if (auto outcome = platform::takeCaptureOutcome()) {
        exportStatus_ = {outcome->ok, outcome->file, outcome->ok ? "(plot PNG)" : outcome->message};
        if (outcome->ok) {
            ctx.log.info("exported " + platform::pathToUtf8(outcome->file));
        } else {
            ctx.log.error("PNG export failed: " + outcome->message);
        }
    }

    const auto result = ctx.results.thermal.get();
    const JobSnapshot job = ctx.jobs.snapshot();
    const bool running = job.state == JobState::Running && job.name == kJob;

    // Badges with tolerances.
    std::vector<ui::Badge> badges;
    {
        ui::Badge mid{"Midpoint error = qh²/8k", ui::Verdict::Pending, "Run the study.",
                      "Pass if |max midpoint error − qh²/(8k)| ≤ " + ui::formatTolerance(kMidTol) +
                          " °C for every N."};
        ui::Badge order{"Order ≈ 2", ui::Verdict::Pending, "Needs ≥ 2 mesh sizes.",
                        "Pass if |observed order − 2| ≤ " + ui::formatRatio(kOrderTol) +
                            " between the two finest meshes."};
        ui::Badge energy{"Energy balance", ui::Verdict::Pending, "Run the study.",
                         "Pass if |heat out left + heat out right − q·A·L| ≤ " +
                             ui::formatTolerance(kEnergyTol) + " W for every N."};
        ui::Badge peak{
            "T_max @ x*", ui::Verdict::Pending, "Run the study.",
            "Pass if the finest mesh's hottest node is within h/2 of the exact x* and its "
            "nodal error ≤ " +
                ui::formatTolerance(kNodalTol) + " °C (brief: 188.2 °C @ 0.42 m)."};
        if (result && !result->rows.empty()) {
            double worstMid = 0.0, worstEnergy = 0.0;
            for (const auto& row : result->rows) {
                worstMid = std::max(worstMid, std::fabs(row.maxMidErr - row.expected));
                worstEnergy = std::max(worstEnergy, std::fabs(row.energyResidual_W));
            }
            mid.verdict = ui::verdictOf(worstMid <= kMidTol);
            mid.measured = "worst |measured − expected| = " + ui::formatFeError(worstMid);
            energy.verdict = ui::verdictOf(worstEnergy <= kEnergyTol);
            energy.measured =
                "worst residual = " + ui::formatTolerance(worstEnergy) + " W (q·A·L = " +
                ui::formatRatio(result->problem.q * result->problem.A * result->problem.L) + " W)";
            if (result->rows.size() >= 2) {
                const auto& last = result->rows.back();
                order.verdict = ui::verdictOf(std::fabs(last.order - 2.0) <= kOrderTol);
                order.measured =
                    "observed order " + ui::formatRatio(last.order) +
                    " (N = " + std::to_string(result->rows[result->rows.size() - 2].N) + " → " +
                    std::to_string(last.N) + "), ratio " + ui::formatRatio(last.ratioToPrev);
            }
            const auto& fine = result->meshes.back();
            const auto fePeak = thermal::nodalPeak(fine.solution);
            const double h = result->rows.back().h;
            const double nodalErr = std::fabs(fePeak.T - fine.report.nodalExact[fePeak.node]);
            peak.verdict =
                ui::verdictOf(std::fabs(fePeak.x - result->exactPeak.x) <= h * 0.5 + 1e-12 &&
                              nodalErr <= kNodalTol);
            peak.measured = "exact " + ui::formatTemperature(result->exactPeak.T) + " @ " +
                            ui::formatCoordinate(result->exactPeak.x) +
                            "; FE (N = " + std::to_string(result->rows.back().N) + ") " +
                            ui::formatTemperature(fePeak.T) + " @ " +
                            ui::formatCoordinate(fePeak.x);
        }
        badges = {mid, order, energy, peak};
    }
    ui::badgeRow(badges);
    ImGui::Dummy(ImVec2(0.0f, t.size.space8));

    const float leftW = 320.0f * t.dpiScale;
    ImGui::BeginChild("##thermLeft", ImVec2(leftW, 0.0f), ImGuiChildFlags_AutoResizeY);
    drawParams(ctx);
    ImGui::EndChild();
    ImGui::SameLine(0.0f, t.size.space16);
    ImGui::BeginChild("##thermRight", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY);
    drawPlots(ctx, result.get(), running);
    ImGui::EndChild();
    ImGui::Dummy(ImVec2(0.0f, t.size.space8));
    drawTable(ctx, result.get());
}

void ThermalView::drawParams(AppContext& ctx) {
    const ui::Theme& t = ui::theme();
    const JobSnapshot job = ctx.jobs.snapshot();
    const bool running = job.state == JobState::Running;

    ui::beginCard("##params");
    ui::cardTitle("Rod parameters");
    ImGui::BeginDisabled(running);
    bool valid = true;
    std::string reason;
    auto field = [&](const ui::ParamSpec& spec, double& v) {
        std::string r;
        if (!ui::paramField(spec, v, r) && valid) {
            valid = false;
            reason = r;
        }
    };
    field({"L", "Length L", "m", 0.0, 1e3, kBrief.L, true}, problem_.L);
    field({"A", "Cross-section A", "m²", 0.0, 1.0, kBrief.A, true}, problem_.A);
    field({"k", "Conductivity k", "W/(m·K)", 0.0, 1e4, kBrief.k, true}, problem_.k);
    field({"q", "Heat generation q", "W/m³", -1e9, 1e9, kBrief.q}, problem_.q);
    field({"T0", "T(0) = T0", "°C", -273.15, 1e4, kBrief.T0}, problem_.T0);
    field({"TL", "T(L) = TL", "°C", -273.15, 1e4, kBrief.TL}, problem_.TL);
    ImGui::Dummy(ImVec2(0.0f, t.size.space4));
    ImGui::TextUnformatted("Meshes N [elements]");
    for (int i = 0; i < 4; ++i) {
        if (i) {
            ImGui::SameLine(0.0f, t.size.space12);
        }
        char label[16];
        std::snprintf(label, sizeof label, "%d", kMeshSizes[i]);
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ui::seriesColor(i));
        ImGui::Checkbox(label, &meshes_[static_cast<std::size_t>(i)]);
        ImGui::PopStyleColor();
    }
    if (ui::secondaryButton("↺ Brief defaults")) {
        problem_ = kBrief;
        meshes_ = {true, true, true, true};
    }
    ImGui::EndDisabled();

    std::vector<int> sizes;
    for (int i = 0; i < 4; ++i) {
        if (meshes_[static_cast<std::size_t>(i)]) {
            sizes.push_back(kMeshSizes[i]);
        }
    }
    if (sizes.empty() && valid) {
        valid = false;
        reason = "Select at least one mesh size.";
    }
    invalidReason_ = reason;
    ImGui::Dummy(ImVec2(0.0f, t.size.space8));
    const ui::RunBarAction action =
        ui::runBar(job, kJob, "Run study", valid, invalidReason_.c_str());
    if (action == ui::RunBarAction::Cancel) {
        ctx.jobs.cancel();
    } else if (action == ui::RunBarAction::Run) {
        const thermal::RodProblem problem = problem_;
        ctx.log.info("thermal study: " + std::to_string(sizes.size()) + " meshes");
        ctx.jobs.submitPublishing(
            kJob, ctx.results.thermal,
            [problem, sizes](JobContext& j) -> std::shared_ptr<const ThermalStudyResult> {
                auto r = runThermalStudy(problem, sizes, j.progressFn(), &j.cancelToken());
                if (!r) {
                    if (r.error().code == ErrorCode::Cancelled) {
                        return nullptr;
                    }
                    throw JobError(r.error().message + " " + r.error().hint);
                }
                return std::move(r).value();
            });
    }
    ui::jobOutcome(job, kJob);
    if (job.name == kJob && job.state == JobState::Failed) {
        ui::inlineAlert(ui::Status::Error, "The study failed", job.message);
    }
    ui::secondaryText("Assumptions: steady state, 1D, constant k and A, insulated lateral surface, "
                      "uniform q; Dirichlet T(0) = T0, T(L) = TL.");
    ui::endCard();
}

void ThermalView::drawPlots(AppContext& ctx, const ThermalStudyResult* r, bool stale) {
    (void)ctx;
    const ui::Theme& t = ui::theme();
    ui::beginCard("##plots");
    if (!r) {
        ui::emptyState("No results yet",
                       "Run the study (Ctrl+Enter) to plot T vs x, the error vs h and the "
                       "midpoint errors for the selected meshes.");
        ui::endCard();
        return;
    }
    if (stale) {
        ui::statusLabel(ui::Status::Info, "Updating…", "showing the last completed study");
    }
    const float plotH = 360.0f * t.dpiScale;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, stale ? 0.55f : 1.0f);
    if (ImGui::BeginTabBar("##plotTabs")) {
        if (ImGui::BeginTabItem("T vs x")) {
            activeTab_ = 0;
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            if (ImPlot::BeginPlot("Temperature along the rod", ImVec2(-1.0f, plotH))) {
                ImPlot::SetupAxes("x [m]", "T [°C]");
                ImPlot::SetupLegend(ImPlotLocation_South);
                for (std::size_t m = 0; m < r->meshes.size(); ++m) {
                    const int n = r->rows[m].N;
                    ImPlotSpec s;
                    s.LineColor = ui::seriesColor(ui::seriesIndexForMesh(n));
                    s.LineWeight = 1.5f * t.dpiScale;
                    s.Marker = n <= 40 ? markerFor(n) : ImPlotMarker_None;
                    s.MarkerSize = 3.0f * t.dpiScale;
                    const std::string label = "FE N = " + std::to_string(n);
                    const auto& sol = r->meshes[m].solution;
                    ImPlot::PlotLine(label.c_str(), sol.x.data(), sol.T.data(),
                                     static_cast<int>(sol.x.size()), s);
                }
                std::vector<double> dx, dy;
                dashed(r->exactX, r->exactT, dx, dy);
                ImPlot::PlotLine("exact T(x)", dx.data(), dy.data(), static_cast<int>(dx.size()),
                                 referenceSpec());
                ImPlot::EndPlot();
            }
            plotRect_ = {pos.x, pos.y, ImGui::GetContentRegionAvail().x, plotH};
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Error vs h")) {
            activeTab_ = 1;
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            if (ImPlot::BeginPlot("Max midpoint error vs element size", ImVec2(-1.0f, plotH))) {
                ImPlot::SetupAxes("h [m]", "max midpoint error [°C]");
                ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
                ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
                ImPlot::SetupLegend(ImPlotLocation_NorthWest);
                std::vector<double> h, e;
                for (const auto& row : r->rows) {
                    h.push_back(row.h);
                    e.push_back(row.maxMidErr);
                }
                ImPlotSpec line;
                line.LineColor = ui::seriesColor(1);
                line.LineWeight = 1.5f * t.dpiScale;
                ImPlot::PlotLine("measured", h.data(), e.data(), static_cast<int>(h.size()), line);
                for (std::size_t i = 0; i < h.size(); ++i) {
                    ImPlotSpec mk;
                    mk.Marker = markerFor(r->rows[i].N);
                    mk.MarkerSize = 5.0f * t.dpiScale;
                    mk.MarkerFillColor = ui::seriesColor(ui::seriesIndexForMesh(r->rows[i].N));
                    mk.MarkerLineColor = mk.MarkerFillColor;
                    mk.Flags = ImPlotItemFlags_NoLegend;
                    ImPlot::PlotScatter("##pt", &h[i], &e[i], 1, mk);
                }
                // Slope-2 guide through the coarsest point, extended half a decade each way.
                if (!h.empty()) {
                    std::vector<double> gh, ge, dh, de;
                    const double h0 = h.front(), e0 = e.front();
                    for (int i = 0; i <= 60; ++i) {
                        const double hh = h0 * 2.0 * std::pow(10.0, -1.6 * i / 60.0);
                        gh.push_back(hh);
                        ge.push_back(e0 * (hh / h0) * (hh / h0));
                    }
                    dashed(gh, ge, dh, de);
                    ImPlot::PlotLine("slope 2 (O(h²))", dh.data(), de.data(),
                                     static_cast<int>(dh.size()), referenceSpec());
                }
                ImPlot::EndPlot();
            }
            plotRect_ = {pos.x, pos.y, ImGui::GetContentRegionAvail().x, plotH};
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Midpoint errors")) {
            activeTab_ = 2;
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            if (ImPlot::BeginPlot("|T_fe − T_exact| at element midpoints", ImVec2(-1.0f, plotH))) {
                ImPlot::SetupAxes("x [m]", "|error| [°C]");
                ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
                ImPlot::SetupLegend(ImPlotLocation_South);
                for (std::size_t m = 0; m < r->meshes.size(); ++m) {
                    const int n = r->rows[m].N;
                    const auto& rep = r->meshes[m].report;
                    std::vector<double> err(rep.midX.size());
                    for (std::size_t i = 0; i < err.size(); ++i) {
                        err[i] = std::fabs(rep.midFe[i] - rep.midExact[i]);
                    }
                    ImPlotSpec s;
                    s.LineColor = ui::seriesColor(ui::seriesIndexForMesh(n));
                    s.LineWeight = 1.5f * t.dpiScale;
                    s.Marker = markerFor(n);
                    s.MarkerSize = 3.0f * t.dpiScale;
                    const std::string label = "N = " + std::to_string(n) + " (qh²/8k = " +
                                              ui::formatFeError(r->rows[m].expected) + ")";
                    ImPlot::PlotLine(label.c_str(), rep.midX.data(), err.data(),
                                     static_cast<int>(err.size()), s);
                }
                ImPlot::EndPlot();
            }
            plotRect_ = {pos.x, pos.y, ImGui::GetContentRegionAvail().x, plotH};
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleVar();
    ui::secondaryText("Study of %zu meshes took %s. Midpoint FE value = ½(T_i + T_i+1); the exact "
                      "solution is drawn dashed on top.",
                      r->rows.size(), ui::formatDuration(r->seconds).c_str());
    ui::endCard();
}

void ThermalView::drawTable(AppContext& ctx, const ThermalStudyResult* r) {
    const ui::Theme& t = ui::theme();
    ui::beginCard("##table");
    ui::cardTitle("Convergence");
    if (!r) {
        ui::secondaryText("No study yet.");
        ui::endCard();
        return;
    }
    const std::vector<ui::TableColumn> cols = {{"N", true},
                                               {"h [m]", true},
                                               {"max mid err [°C]", true},
                                               {"qh²/8k [°C]", true},
                                               {"ratio", true},
                                               {"order", true},
                                               {"max nodal err [°C]", true},
                                               {"energy residual [W]", true}};
    std::vector<ui::TableRow> rows;
    for (const auto& row : r->rows) {
        ui::TableRow tr;
        auto cell = [&](std::string text, double key) {
            tr.cells.push_back({std::move(text), key});
        };
        char buf[48];
        cell(std::to_string(row.N), row.N);
        std::snprintf(buf, sizeof buf, "%.6g", row.h);
        cell(buf, row.h);
        cell(ui::formatFeError(row.maxMidErr), row.maxMidErr);
        cell(ui::formatFeError(row.expected), row.expected);
        cell(ui::formatRatio(row.ratioToPrev),
             std::isfinite(row.ratioToPrev) ? row.ratioToPrev : -1.0);
        cell(ui::formatRatio(row.order), std::isfinite(row.order) ? row.order : -1.0);
        cell(ui::formatFeError(row.maxNodalErr), row.maxNodalErr);
        std::snprintf(buf, sizeof buf, "%.2e", row.energyResidual_W);
        cell(buf, row.energyResidual_W);
        tr.status = std::fabs(row.maxMidErr - row.expected) <= kMidTol ? ui::Status::Success
                                                                       : ui::Status::Error;
        rows.push_back(std::move(tr));
    }
    ui::dataTable("##conv", cols, rows, true);
    ui::secondaryText("pass = |max mid err − qh²/8k| ≤ %s °C. ratio and order compare each row "
                      "with the previous one.",
                      ui::formatTolerance(kMidTol).c_str());
    ImGui::Dummy(ImVec2(0.0f, t.size.space4));
    const int choice =
        ui::exportMenu("thermalExport",
                       {"Convergence table (convergence.csv)", "Nodal temperatures, all N (CSV)",
                        "Midpoint errors, all N (CSV)", "Current plot (PNG)"},
                       true, nullptr);
    if (choice >= 0) {
        exportChoice(ctx, *r, choice);
    }
    ImGui::SameLine(0.0f, t.size.space12);
    ui::exportStatusLine(exportStatus_);
    ui::endCard();
}

void ThermalView::exportChoice(AppContext& ctx, const ThermalStudyResult& r, int choice) {
    static const wchar_t* const kNames[] = {L"convergence.csv", L"fea_nodes_all_N.csv",
                                            L"fea_midpoints_all_N.csv", L"thermal_plot.png"};
    static const wchar_t* const kPlotNames[] = {L"T_vs_x.png", L"error_vs_h_loglog.png",
                                                L"midpoint_errors.png"};
    const bool png = choice == 3;
    const std::vector<platform::FileFilter> filters =
        png ? std::vector<platform::FileFilter>{{L"PNG image (*.png)", L"*.png"}}
            : std::vector<platform::FileFilter>{{L"CSV (*.csv)", L"*.csv"}};
    const auto file = platform::saveFileDialog(
        ctx.window, filters, png ? kPlotNames[activeTab_] : kNames[choice], png ? L"png" : L"csv");
    if (!file) {
        return;
    }
    if (png) {
        pngPending_ = *file; // captured next frame, after the menu has closed
        return;
    }
    Result<void> saved = choice == 0   ? exportConvergenceCsv(r, *file)
                         : choice == 1 ? exportNodesCsv(r, *file)
                                       : exportMidpointsCsv(r, *file);
    if (saved) {
        std::size_t rowsOut = r.rows.size();
        if (choice == 1) {
            rowsOut = 0;
            for (const auto& m : r.meshes) {
                rowsOut += m.solution.x.size();
            }
        } else if (choice == 2) {
            rowsOut = 0;
            for (const auto& m : r.meshes) {
                rowsOut += m.report.midX.size();
            }
        }
        exportStatus_ = {true, *file, "(" + ui::formatCount(rowsOut) + " rows)"};
        ctx.log.info("exported " + platform::pathToUtf8(*file));
    } else {
        exportStatus_ = {false, *file, saved.error().message + " " + saved.error().hint};
        ctx.log.error("export failed: " + saved.error().message);
    }
}

} // namespace uniforge::views
