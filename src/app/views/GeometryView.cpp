#include "app/views/GeometryView.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

#include <imgui.h>

#include "app/platform/FileDialog.hpp"
#include "app/platform/Text.hpp"
#include "app/services/JobRunner.hpp"
#include "app/services/LogSink.hpp"
#include "app/services/ResultStore.hpp"
#include "app/ui/Format.hpp"
#include "app/ui/Theme.hpp"
#include "app/ui/widgets/DataViews.hpp"
#include "app/ui/widgets/Forms.hpp"
#include "app/ui/widgets/Widgets.hpp"

namespace uniforge::views {

namespace {

constexpr const char* kStlJob = "geometry.import.stl";
constexpr const char* kCsvJob = "geometry.import.csv";
constexpr const char* kSampleStl = "geometry/rod_10x10mm_1m.stl";
constexpr const char* kSampleCsv = "thermal/rod_nodal_temperature_SYNTHETIC.csv";

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string bboxText(const io::Vec3d& a, const io::Vec3d& b) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%.4g × %.4g × %.4g m", b.x - a.x, b.y - a.y, b.z - a.z);
    return buf;
}

} // namespace

std::filesystem::path GeometryView::findDataDir(const std::filesystem::path& start) {
    std::error_code ec;
    std::filesystem::path dir = start;
    for (int i = 0; i < 6 && !dir.empty(); ++i) {
        if (std::filesystem::exists(dir / "data" / kSampleStl, ec)) {
            return dir / "data";
        }
        const auto parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
    return {};
}

void GeometryView::queueImport(Kind kind, std::filesystem::path file) {
    (kind == Kind::Stl ? pendingStl_ : pendingCsv_) = file;
    queue_.push_back({kind, std::move(file)});
}

void GeometryView::pumpQueue(AppContext& ctx) {
    if (queue_.empty() || ctx.jobs.busy()) {
        return;
    }
    const Import next = queue_.front();
    queue_.pop_front();
    auto failure = failure_;
    const auto file = next.file;
    const std::string name = platform::pathToUtf8(file.filename());
    ctx.log.info(std::string("import ") + (next.kind == Kind::Stl ? "STL " : "CSV ") + name);

    // On failure the job stores the typed Error (message + hint for the alert) and throws, so
    // the runner reports Failed and publishes nothing: the previous model stays.
    auto fail = [failure, kind = next.kind](Error e) {
        std::lock_guard<std::mutex> lock(failure->mutex);
        const std::string text = e.message;
        failure->error = std::move(e);
        failure->kind = kind;
        throw JobError(text);
    };
    if (next.kind == Kind::Stl) {
        ctx.jobs.submitPublishing(
            kStlJob, ctx.results.geometryMesh,
            [file, fail](JobContext& job) -> std::shared_ptr<const MeshModel> {
                auto r = importMeshModel(file, {}, job.progressFn(), &job.cancelToken());
                if (!r) {
                    if (r.error().code == ErrorCode::Cancelled) {
                        return nullptr;
                    }
                    fail(std::move(r).error());
                }
                return std::move(r).value();
            });
    } else {
        ctx.jobs.submitPublishing(
            kCsvJob, ctx.results.geometryPoints,
            [file, fail](JobContext& job) -> std::shared_ptr<const PointModel> {
                auto r = importPointModel(file, {}, job.progressFn(), &job.cancelToken());
                if (!r) {
                    if (r.error().code == ErrorCode::Cancelled) {
                        return nullptr;
                    }
                    fail(std::move(r).error());
                }
                return std::move(r).value();
            });
    }
}

void GeometryView::browse(AppContext& ctx, Kind kind) {
    const std::vector<platform::FileFilter> filters =
        kind == Kind::Stl ? std::vector<platform::FileFilter>{{L"ASCII STL (*.stl)", L"*.stl"},
                                                              {L"All files", L"*.*"}}
                          : std::vector<platform::FileFilter>{
                                {L"Nodal temperatures (*.csv)", L"*.csv"}, {L"All files", L"*.*"}};
    const auto dataDir = findDataDir(ctx.runtime.paths.packageDir);
    if (auto file = platform::openFileDialog(
            ctx.window, filters,
            dataDir.empty() ? dataDir : dataDir / (kind == Kind::Stl ? "geometry" : "thermal"))) {
        queueImport(kind, *file);
    }
}

void GeometryView::draw(AppContext& ctx) {
    const ui::Theme& t = ui::theme();

    // Drag-drop from Explorer: route by extension.
    for (const auto& file : ctx.droppedFiles) {
        const std::string ext = lower(platform::pathToUtf8(file.extension()));
        queueImport(ext == ".csv" ? Kind::Csv : Kind::Stl, file);
    }
    ctx.droppedFiles.clear();

    // Pick up the error of a newly failed import.
    const JobSnapshot job = ctx.jobs.snapshot();
    const bool importJob = job.name == kStlJob || job.name == kCsvJob;
    if (importJob && job.state == JobState::Failed && job.id != lastFailedJob_) {
        lastFailedJob_ = job.id;
        std::lock_guard<std::mutex> lock(failure_->mutex);
        shownError_ = failure_->error;
        shownErrorKind_ = failure_->kind;
        if (shownError_) {
            ctx.log.warn(std::string("import failed [") + std::string(toString(shownError_->code)) +
                         "] " + shownError_->message);
        }
    } else if (importJob && job.state == JobState::Succeeded && shownError_) {
        const Kind done = job.name == kStlJob ? Kind::Stl : Kind::Csv;
        if (done == shownErrorKind_) {
            shownError_.reset();
        }
    }
    pumpQueue(ctx);

    // Badges (spec §8.6): STL valid, CSV valid, non-finite rejected.
    const auto mesh = ctx.results.geometryMesh.get();
    const auto points = ctx.results.geometryPoints.get();
    std::vector<ui::Badge> badges;
    {
        ui::Badge b{
            "STL valid", ui::Verdict::Pending, "No STL loaded yet.",
            "Pass: ASCII STL parsed with ≥ 1 facet, every vertex finite, 3 vertices per facet."};
        if (mesh) {
            b.verdict = ui::Verdict::Pass;
            b.measured = ui::formatCount(mesh->source.tri.size()) + " facets · " +
                         ui::formatCount(mesh->source.degenerate) + " degenerate · " +
                         ui::formatCount(mesh->source.recomputedNormals) + " normals recomputed";
        }
        badges.push_back(std::move(b));
    }
    {
        ui::Badge b{
            "CSV valid", ui::Verdict::Pending, "No CSV loaded yet.",
            "Pass: exact header node_id,x_m,y_m,z_m,temperature_C, unique ids, T ≥ −273.15 °C."};
        if (points) {
            b.verdict = ui::Verdict::Pass;
            b.measured = ui::formatCount(points->source.nodes.size()) + " nodes · T " +
                         ui::formatTemperature(points->source.Tmin) + " … " +
                         ui::formatTemperature(points->source.Tmax);
        }
        badges.push_back(std::move(b));
    }
    {
        ui::Badge b{"Non-finite rejected", ui::Verdict::Pending, "Nothing imported yet.",
                    "nan / inf / out-of-range numbers fail the import with the line number "
                    "(tests/negative/io fixtures)."};
        if (mesh || points) {
            b.verdict = ui::Verdict::Pass;
            b.measured =
                "Every imported coordinate and temperature is finite (checked while parsing).";
        }
        if (shownError_ && shownError_->code == ErrorCode::NonFinite) {
            b.verdict = ui::Verdict::Pass;
            b.measured = "Last import was rejected: " + shownError_->message;
        }
        badges.push_back(std::move(b));
    }
    ui::badgeRow(badges);
    ImGui::Dummy(ImVec2(0.0f, t.size.space8));

    const float leftW = 340.0f * t.dpiScale;
    const float height =
        std::max(ImGui::GetContentRegionAvail().y - t.size.space8, 520.0f * t.dpiScale);
    ImGui::BeginChild("##geomLeft", ImVec2(leftW, height));
    drawControls(ctx);
    ImGui::EndChild();
    ImGui::SameLine(0.0f, t.size.space16);
    ImGui::BeginChild("##geomRight", ImVec2(0.0f, height));
    ui::ViewportScene scene{mesh, points, showMesh_, showPoints_, pointSize_ * t.dpiScale};
    viewport_.draw(ctx, scene, ImGui::GetContentRegionAvail());
    ImGui::EndChild();
}

void GeometryView::drawControls(AppContext& ctx) {
    const ui::Theme& t = ui::theme();
    const JobSnapshot job = ctx.jobs.snapshot();
    const bool importing =
        (job.name == kStlJob || job.name == kCsvJob) && job.state == JobState::Running;
    const bool busy = ctx.jobs.busy() || !queue_.empty();
    const auto mesh = ctx.results.geometryMesh.get();
    const auto points = ctx.results.geometryPoints.get();

    ui::beginCard("##files");
    ui::cardTitle("Files");
    if (ui::fileField("stl", "Geometry: ASCII STL [m]", mesh ? mesh->file : std::filesystem::path(),
                      !busy, "An import is running") == ui::FileFieldAction::Browse) {
        browse(ctx, Kind::Stl);
    }
    ImGui::Dummy(ImVec2(0.0f, t.size.space4));
    if (ui::fileField("csv", "Nodal temperatures: CSV [m, °C]",
                      points ? points->file : std::filesystem::path(), !busy,
                      "An import is running") == ui::FileFieldAction::Browse) {
        browse(ctx, Kind::Csv);
    }
    if (points && points->source.syntheticFlag) {
        ImGui::Dummy(ImVec2(0.0f, t.size.space4));
        ui::chip("◇ SYNTHETIC DATA", ui::statusColor(ui::Status::Simulated));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Self-generated sample (file name contains SYNTHETIC), not an ANSYS export. "
                "See data/PROVENANCE.md.");
        }
    }
    ImGui::Dummy(ImVec2(0.0f, t.size.space8));
    const auto dataDir = findDataDir(ctx.runtime.paths.packageDir);
    if (ui::secondaryButton("Load samples", !busy && !dataDir.empty(),
                            busy ? "An import is running"
                                 : "data/ folder not found next to the app")) {
        queueImport(Kind::Stl, dataDir / kSampleStl);
        queueImport(Kind::Csv, dataDir / kSampleCsv);
    }
    if (ImGui::IsItemHovered() && !dataDir.empty()) {
        ImGui::SetTooltip("rod_10x10mm_1m.stl + rod_nodal_temperature_SYNTHETIC.csv from data/");
    }
    ImGui::SameLine(0.0f, t.size.space8);
    ui::secondaryText("or drop .stl / .csv files on the window");

    // Import progress (same RunBar pattern, Run replaced by the file fields).
    if (importing) {
        ImGui::Dummy(ImVec2(0.0f, t.size.space4));
        if (ui::secondaryButton(job.cancelRequested ? "Cancelling…" : "Cancel  Esc",
                                !job.cancelRequested) ||
            (!job.cancelRequested &&
             ImGui::Shortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteGlobal))) {
            ctx.jobs.cancel();
            queue_.clear();
        }
        ImGui::SameLine(0.0f, t.size.space8);
        ImGui::ProgressBar(static_cast<float>(job.fraction), ImVec2(-1.0f, 0.0f));
        ui::secondaryText("%s · %s", job.stage.c_str(),
                          ui::formatDuration(job.elapsedSeconds).c_str());
    } else if (job.name == kStlJob || job.name == kCsvJob) {
        ui::jobOutcome(job, job.name.c_str());
    }
    if (shownError_) {
        ImGui::Dummy(ImVec2(0.0f, t.size.space4));
        std::string title = shownError_->message;
        if (shownError_->line && title.find("Line") == std::string::npos) {
            title = "Line " + std::to_string(*shownError_->line) + ": " + title;
        }
        ui::inlineAlert(ui::Status::Error, title,
                        shownError_->hint +
                            (mesh || points ? " The previous data is still shown." : ""));
    }
    ui::endCard();

    ImGui::Dummy(ImVec2(0.0f, t.size.space8));
    ui::beginCard("##display");
    ui::cardTitle("Display");
    ImGui::BeginDisabled(!mesh);
    ImGui::Checkbox("Show mesh", &showMesh_);
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, t.size.space16);
    ImGui::BeginDisabled(!points);
    ImGui::Checkbox("Show points", &showPoints_);
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    ImGui::SliderFloat("Point size [px]", &pointSize_, 2.0f, 24.0f, "%.0f");
    drawSummary(ctx);
    ui::endCard();

    ImGui::Dummy(ImVec2(0.0f, t.size.space8));
    ui::beginCard("##inspector");
    ui::cardTitle("Node inspector");
    ui::InspectedNode node;
    const auto pickedIndex = viewport_.picked();
    const bool havePick = points && pickedIndex && *pickedIndex < points->source.nodes.size();
    if (havePick) {
        const io::ThermalNode& n = points->source.nodes[*pickedIndex];
        node = {static_cast<long long>(n.id), n.p.x, n.p.y, n.p.z, n.T_C};
    }
    ui::nodeInspector(havePick ? &node : nullptr);
    ui::endCard();
}

void GeometryView::drawSummary(AppContext& ctx) {
    (void)ctx;
    const auto mesh = ctx.results.geometryMesh.get();
    const auto points = ctx.results.geometryPoints.get();
    ImGui::Dummy(ImVec2(0.0f, ui::theme().size.space4));
    if (mesh) {
        const auto& s = mesh->source;
        ui::secondaryText("STL: %s facets · %s degenerate · bbox %s · %s · %s",
                          ui::formatCount(s.tri.size()).c_str(),
                          ui::formatCount(s.degenerate).c_str(), bboxText(s.bbMin, s.bbMax).c_str(),
                          ui::formatBytes(s.stats.bytes).c_str(),
                          ui::formatDuration(s.stats.parseSeconds).c_str());
        for (const auto& w : s.stats.warnings) {
            ui::statusLabel(ui::Status::Warning, "Warning", w.c_str());
        }
    }
    if (points) {
        const auto& s = points->source;
        ui::secondaryText("CSV: %s nodes · T %s … %s · bbox %s · %s · %s",
                          ui::formatCount(s.nodes.size()).c_str(),
                          ui::formatTemperature(s.Tmin).c_str(),
                          ui::formatTemperature(s.Tmax).c_str(), bboxText(s.bbMin, s.bbMax).c_str(),
                          ui::formatBytes(s.stats.bytes).c_str(),
                          ui::formatDuration(s.stats.parseSeconds).c_str());
        for (const auto& w : s.stats.warnings) {
            ui::statusLabel(ui::Status::Warning, "Warning", w.c_str());
        }
    }
    if (!mesh && !points) {
        ui::secondaryText("Nothing loaded.");
    }
    ui::secondaryText("Viewport: MSAA %d×", viewport_.msaaSamples());
}

} // namespace uniforge::views
