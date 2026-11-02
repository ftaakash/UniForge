#include "app/ui/widgets/Forms.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <imgui.h>

#include "app/platform/Text.hpp"
#include "app/ui/Fonts.hpp"
#include "app/ui/Format.hpp"
#include "app/ui/widgets/Widgets.hpp"

namespace uniforge::ui {

namespace {

Status statusOf(Verdict v) {
    switch (v) {
    case Verdict::Pass:
        return Status::Success;
    case Verdict::Fail:
        return Status::Error;
    case Verdict::Pending:
    case Verdict::NotApplicable:
        break;
    }
    return Status::Pending;
}

const char* verdictWord(Verdict v) {
    switch (v) {
    case Verdict::Pass:
        return "pass";
    case Verdict::Fail:
        return "fail";
    case Verdict::Pending:
        return "pending";
    case Verdict::NotApplicable:
        return "n/a";
    }
    return "";
}

ImVec2 badgeSize(const Badge& b) {
    const Theme& t = theme();
    char text[160];
    std::snprintf(text, sizeof text, "%s %s", statusGlyph(statusOf(b.verdict)), b.label.c_str());
    ImGui::PushFont(fonts().ui, t.type.label);
    const ImVec2 s = ImGui::CalcTextSize(text);
    ImGui::PopFont();
    return ImVec2(s.x + t.size.space8 * 2.0f, s.y + t.size.space4 * 1.5f);
}

} // namespace

Verdict verdictOf(bool pass) {
    return pass ? Verdict::Pass : Verdict::Fail;
}

void verificationBadge(const Badge& b) {
    const Theme& t = theme();
    const Status status = statusOf(b.verdict);
    const ImVec4& color = b.verdict == Verdict::Pending || b.verdict == Verdict::NotApplicable
                              ? t.color.textSecondary
                              : statusColor(status);
    char text[160];
    std::snprintf(text, sizeof text, "%s %s", statusGlyph(status), b.label.c_str());
    ImGui::PushID(b.label.c_str());
    {
        ScopedFont font(fonts().ui, t.type.label);
        const ImVec2 size = badgeSize(b);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                          ImGui::GetColorU32(withAlpha(color, 0.10f)), t.size.radiusControl);
        dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                    ImGui::GetColorU32(withAlpha(color, 0.55f)), t.size.radiusControl);
        dl->AddText(ImVec2(pos.x + t.size.space8, pos.y + t.size.space4 * 0.75f),
                    ImGui::GetColorU32(color), text);
        ImGui::InvisibleButton("##badge", size);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("%s — %s", b.label.c_str(), verdictWord(b.verdict));
        if (!b.measured.empty()) {
            ImGui::TextUnformatted(b.measured.c_str());
        }
        if (!b.tolerance.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, t.color.textSecondary);
            ImGui::TextUnformatted(b.tolerance.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::EndTooltip();
    }
    ImGui::PopID();
}

void badgeRow(const std::vector<Badge>& badges) {
    const Theme& t = theme();
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    for (std::size_t i = 0; i < badges.size(); ++i) {
        if (i > 0) {
            ImGui::SameLine(0.0f, t.size.space8);
            if (ImGui::GetCursorScreenPos().x + badgeSize(badges[i]).x > right) {
                ImGui::NewLine();
            }
        }
        verificationBadge(badges[i]);
    }
}

void inlineAlert(Status status, const std::string& title, const std::string& body) {
    const Theme& t = theme();
    const ImVec4& color = statusColor(status);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, withAlpha(color, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_Border, withAlpha(color, 0.6f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(t.size.space12, t.size.space8));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, t.size.radiusControl);
    ImGui::PushID(title.c_str());
    ImGui::BeginChild("##alert", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                          ImGuiChildFlags_AlwaysUseWindowPadding);
    {
        ScopedFont font(fonts().uiSemiBold, t.type.body);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s %s", statusGlyph(status), title.c_str());
        ImGui::PopStyleColor();
    }
    if (!body.empty()) {
        ImGui::TextWrapped("%s", body.c_str());
    }
    ImGui::EndChild();
    ImGui::PopID();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

bool paramField(const ParamSpec& spec, double& value, std::string& reason) {
    const Theme& t = theme();
    ImGui::PushID(spec.id);
    const bool belowMin = spec.minExclusive ? !(value > spec.min) : !(value >= spec.min);
    const bool valid = std::isfinite(value) && !belowMin && value <= spec.max;

    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s [%s]", spec.label, spec.unit);
    const float resetW = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - resetW - t.size.space4);
    if (!valid) {
        ImGui::PushStyleColor(ImGuiCol_Border, statusColor(Status::Error));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f * t.dpiScale);
    }
    ImGui::InputDouble("##v", &value, 0.0, 0.0, spec.format);
    if (!valid) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    ImGui::SameLine(0.0f, t.size.space4);
    const bool isDefault = value == spec.defaultValue;
    ImGui::BeginDisabled(isDefault);
    if (ImGui::Button("↺", ImVec2(resetW, 0.0f))) {
        value = spec.defaultValue;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        char tip[128];
        std::snprintf(tip, sizeof tip, isDefault ? "Brief default %.6g %s" : "Reset to %.6g %s",
                      spec.defaultValue, spec.unit);
        ImGui::SetTooltip("%s", tip);
    }

    if (!valid) {
        char buf[200];
        std::snprintf(buf, sizeof buf, "%s must be %s %.6g and ≤ %.6g %s.", spec.label,
                      spec.minExclusive ? ">" : "≥", spec.min, spec.max, spec.unit);
        reason = buf;
        ImGui::PushStyleColor(ImGuiCol_Text, statusColor(Status::Error));
        ImGui::TextWrapped("%s %s", statusGlyph(Status::Error), buf);
        ImGui::PopStyleColor();
    }
    ImGui::PopID();
    return valid;
}

FileFieldAction fileField(const char* id, const char* label, const std::filesystem::path& path,
                          bool enabled, const char* disabledReason) {
    const Theme& t = theme();
    FileFieldAction action = FileFieldAction::None;
    ImGui::PushID(id);
    ImGui::TextUnformatted(label);
    const std::string name = path.empty() ? std::string("No file — browse or drop one here")
                                          : platform::pathToUtf8(path.filename());
    const float browseW =
        ImGui::CalcTextSize("Browse…").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseW - t.size.space4);
    ImGui::PushStyleColor(ImGuiCol_Text,
                          path.empty() ? t.color.textSecondary : t.color.textPrimary);
    std::string shown = name;
    ImGui::InputText("##path", shown.data(), shown.size() + 1, ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleColor();
    if (!path.empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", platform::pathToUtf8(path).c_str());
    }
    ImGui::SameLine(0.0f, t.size.space4);
    if (secondaryButton("Browse…", enabled, disabledReason)) {
        action = FileFieldAction::Browse;
    }
    ImGui::PopID();
    return action;
}

RunBarAction runBar(const JobSnapshot& job, const char* jobName, const char* runLabel, bool canRun,
                    const char* disabledReason) {
    const Theme& t = theme();
    RunBarAction action = RunBarAction::None;
    const bool running = job.state == JobState::Running;
    const bool mine = job.name == jobName;
    if (running && mine) {
        const bool cancelling = job.cancelRequested;
        if (secondaryButton(cancelling ? "Cancelling…" : "Cancel  Esc", !cancelling,
                            "Cancel already requested") ||
            (!cancelling && ImGui::Shortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteGlobal))) {
            action = RunBarAction::Cancel;
        }
    } else {
        const bool enabled = canRun && !running;
        const char* reason =
            running ? "Another job is running; wait for it or cancel it" : disabledReason;
        char label[96];
        std::snprintf(label, sizeof label, "%s  Ctrl+Enter", runLabel);
        if (primaryButton(label, enabled, reason) ||
            (enabled &&
             ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Enter, ImGuiInputFlags_RouteGlobal))) {
            action = RunBarAction::Run;
        }
    }
    // In a narrow panel the bar wraps to its own line and shrinks to fit; a bar wider than
    // the panel gets clipped and loses its "100 %" label.
    ImGui::SameLine(0.0f, t.size.space12);
    float barWidth = ImGui::GetContentRegionAvail().x;
    if (barWidth < 120.0f * t.dpiScale) {
        ImGui::NewLine();
        barWidth = ImGui::GetContentRegionAvail().x;
    }
    barWidth = std::min(barWidth, 220.0f * t.dpiScale);
    const float fraction = mine ? static_cast<float>(job.fraction) : 0.0f;
    char overlay[32];
    std::snprintf(overlay, sizeof overlay, "%.0f %%", fraction * 100.0f);
    ImGui::ProgressBar(fraction, ImVec2(barWidth, 0.0f), overlay);
    if (mine && running) {
        ImGui::SameLine(0.0f, t.size.space12);
        ImGui::AlignTextToFramePadding();
        secondaryText("%s · %s", job.stage.c_str(), formatDuration(job.elapsedSeconds).c_str());
    }
    return action;
}

void jobOutcome(const JobSnapshot& job, const char* jobName) {
    if (job.name != jobName) {
        return;
    }
    switch (job.state) {
    case JobState::Succeeded: {
        const std::string done = "in " + formatDuration(job.elapsedSeconds);
        statusLabel(Status::Success, "Done", done.c_str());
        break;
    }
    case JobState::Cancelled:
        statusLabel(Status::Warning, "Cancelled", "previous results kept");
        break;
    case JobState::Failed:
        statusLabel(Status::Error, "Failed", "see the message below; previous results kept");
        break;
    case JobState::Running:
    case JobState::Idle:
        break;
    }
}

} // namespace uniforge::ui
