#include "app/ui/widgets/LogPanel.hpp"

#include <algorithm>
#include <cctype>
#include <string>

#include <imgui.h>

#include "app/platform/Paths.hpp"
#include "app/ui/Fonts.hpp"
#include "app/ui/Theme.hpp"
#include "app/ui/widgets/Widgets.hpp"

namespace uniforge::ui {

namespace {

const char* const kLevelNames[] = {"All", "Info and above", "Warnings and errors", "Errors only"};

bool containsIgnoreCase(const std::string& haystack, const char* needle) {
    const std::string n(needle);
    if (n.empty()) {
        return true;
    }
    const auto lower = [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    return std::search(haystack.begin(), haystack.end(), n.begin(), n.end(),
                       [&](char a, char b) { return lower(a) == lower(b); }) != haystack.end();
}

const ImVec4& levelColor(LogLevel level) {
    const Theme& t = theme();
    switch (level) {
    case LogLevel::Debug:
        return t.color.textDisabled;
    case LogLevel::Warn:
        return t.color.warning;
    case LogLevel::Error:
        return t.color.error;
    case LogLevel::Info:
        break;
    }
    return t.color.textPrimary;
}

} // namespace

bool LogPanel::passesFilter(const LogEntry& e) const {
    return static_cast<int>(e.level) >= minLevel_ && containsIgnoreCase(e.message, search_);
}

void LogPanel::refresh(const LogSink& log) {
    if (log.version() != seenVersion_) {
        seenVersion_ = log.version();
        entries_ = log.snapshot();
        filterDirty_ = true;
    }
    if (filterDirty_) {
        shown_.clear();
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
            if (passesFilter(entries_[static_cast<std::size_t>(i)])) {
                shown_.push_back(i);
            }
        }
        filterDirty_ = false;
    }
}

void LogPanel::drawToolbar(const AppContext& ctx) {
    const Theme& t = theme();
    cardTitle("Log");
    ImGui::SameLine(0.0f, t.size.space16);

    ImGui::SetNextItemWidth(170.0f * t.dpiScale);
    if (ImGui::Combo("##level", &minLevel_, kLevelNames, IM_ARRAYSIZE(kLevelNames))) {
        filterDirty_ = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f * t.dpiScale);
    if (ImGui::InputTextWithHint("##search", "Search log", search_, sizeof search_)) {
        filterDirty_ = true;
    }
    ImGui::SameLine();
    if (secondaryButton("Copy", !shown_.empty(), "Nothing to copy with the current filter")) {
        std::string text;
        for (int i : shown_) {
            text += formatLogLine(entries_[static_cast<std::size_t>(i)]) + "\n";
        }
        ImGui::SetClipboardText(text.c_str());
    }
    ImGui::SameLine();
    if (secondaryButton("Open folder")) {
        platform::openInExplorer(ctx.runtime.paths.logsDir);
    }
    ImGui::SameLine();
    if (secondaryButton("Hide  Ctrl+L")) {
        visible_ = false;
    }
}

void LogPanel::drawLines() {
    const Theme& t = theme();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, t.color.bgSurfaceAlt);
    ImGui::BeginChild("##loglines", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    {
        ScopedFont font(fonts().mono, t.type.mono);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(t.size.space8, 2.0f * t.dpiScale));
        const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(shown_.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const LogEntry& e =
                    entries_[static_cast<std::size_t>(shown_[static_cast<std::size_t>(row)])];
                // Time of day in UTC, matching the log file ("10:22:04.512Z").
                const std::string stamp = formatTimestampUtc(e.time).substr(11);
                ImGui::PushStyleColor(ImGuiCol_Text, t.color.textSecondary);
                ImGui::TextUnformatted(stamp.c_str());
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, levelColor(e.level));
                ImGui::Text("%-5s %s", std::string(toString(e.level)).c_str(), e.message.c_str());
                ImGui::PopStyleColor();
            }
        }
        if (scrollToBottom_ || atBottom) { // follow new lines unless the user scrolled up
            ImGui::SetScrollHereY(1.0f);
            scrollToBottom_ = false;
        }
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void LogPanel::draw(const AppContext& ctx, float width, float height) {
    const Theme& t = theme();
    refresh(ctx.log);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, t.color.bgSurface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(t.size.space12, t.size.space8));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::BeginChild("##logpanel", ImVec2(width, height),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    drawToolbar(ctx);
    drawLines();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

} // namespace uniforge::ui
