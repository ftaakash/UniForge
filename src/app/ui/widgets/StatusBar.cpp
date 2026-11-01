#include "app/ui/widgets/StatusBar.hpp"

#include <cstdio>
#include <string>

#include <imgui.h>

#include "app/services/JobRunner.hpp"
#include "app/ui/Fonts.hpp"
#include "app/ui/Theme.hpp"

namespace uniforge::ui {

namespace {

void separator() {
    const Theme& t = theme();
    ImGui::SameLine(0.0f, t.size.space12);
    ImGui::PushStyleColor(ImGuiCol_Text, t.color.textDisabled);
    ImGui::TextUnformatted("·");
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, t.size.space12);
}

void coloured(const ImVec4& color, const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
}

void drawJobState(const JobSnapshot& job) {
    const Theme& t = theme();
    char buf[256];
    switch (job.state) {
    case JobState::Idle:
        coloured(t.color.textSecondary, "● Idle");
        return;
    case JobState::Running:
        std::snprintf(buf, sizeof buf, "● %s %s · %s · %.0f %% · %.1f s",
                      job.cancelRequested ? "Cancelling" : "Running", job.name.c_str(),
                      job.stage.c_str(), job.fraction * 100.0, job.elapsedSeconds);
        coloured(t.color.info, buf);
        return;
    case JobState::Succeeded:
        std::snprintf(buf, sizeof buf, "%s %s · %s", statusGlyph(Status::Success), job.name.c_str(),
                      job.message.c_str());
        coloured(statusColor(Status::Success), buf);
        return;
    case JobState::Cancelled:
        std::snprintf(buf, sizeof buf, "%s %s · %s", statusGlyph(Status::Warning), job.name.c_str(),
                      job.message.c_str());
        coloured(statusColor(Status::Warning), buf);
        return;
    case JobState::Failed:
        std::snprintf(buf, sizeof buf, "%s %s failed: %s", statusGlyph(Status::Error),
                      job.name.c_str(), job.message.c_str());
        coloured(statusColor(Status::Error), buf);
        return;
    }
}

void drawRenderBackend(const platform::RenderInfo& render) {
    const Theme& t = theme();
    if (render.software()) {
        coloured(statusColor(Status::Warning), "! D3D11 WARP · Software rendering");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("No hardware Direct3D 11 device: %s", render.hardwareError.c_str());
        }
        return;
    }
    coloured(t.color.textSecondary, "D3D11 HW");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", render.describe().c_str());
    }
}

void drawLaunchTime(const platform::LaunchTiming& launch) {
    const double headline = launch.fromLauncherSeconds ? *launch.fromLauncherSeconds
                                                       : launch.fromProcessSeconds.value_or(0.0);
    char buf[64];
    std::snprintf(buf, sizeof buf, "Ready in %.2f s", headline);
    coloured(theme().color.textSecondary, buf);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", launch.fromLauncherSeconds
                                    ? "From Launch.exe start to the first presented frame"
                                    : "From UniForge.exe start to the first presented frame "
                                      "(started without Launch.exe)");
    }
}

void drawContents(const AppContext& ctx, float height) {
    const Theme& t = theme();
    ScopedFont font(fonts().ui, t.type.label);
    ImGui::SetCursorPosY((height - ImGui::GetTextLineHeight()) * 0.5f);

    drawJobState(ctx.jobs.snapshot());
    separator();
    drawRenderBackend(ctx.runtime.render);
    separator();
    coloured(t.color.textSecondary, std::to_string(ctx.runtime.logicalCpus) + " CPUs");
    if (ctx.runtime.launchMeasured) {
        separator();
        drawLaunchTime(ctx.runtime.launch);
    }
    if (!ctx.runtime.paths.packageWritable) {
        separator();
        coloured(statusColor(Status::Info), "i Read-only package: logs in %LOCALAPPDATA%");
    }
}

} // namespace

void drawStatusBar(const AppContext& ctx, float width, float height) {
    const Theme& t = theme();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, t.color.bgSurfaceAlt);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(t.size.space12, 0.0f));
    ImGui::BeginChild("##statusbar", ImVec2(width, height), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav);
    drawContents(ctx, height);
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

} // namespace uniforge::ui
