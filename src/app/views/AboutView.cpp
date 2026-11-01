#include "app/views/AboutView.hpp"

#include <cstdio>
#include <optional>
#include <string>

#include <imgui.h>

#include "app/BuildInfo.hpp"
#include "app/platform/Paths.hpp"
#include "app/platform/Text.hpp"
#include "app/ui/Fonts.hpp"
#include "app/ui/Theme.hpp"
#include "app/ui/widgets/Widgets.hpp"

namespace uniforge::views {

namespace {

struct Licence {
    const char* name;
    const char* version;
    const char* licence;
};

constexpr Licence kLicences[] = {
    {"Dear ImGui (docking)", "1.93.0", "MIT"},
    {"ImPlot", "1.1", "MIT"},
    {"Inter", "4.1", "SIL OFL 1.1"},
    {"JetBrains Mono", "2.304", "SIL OFL 1.1"},
    {"GoogleTest (tests only)", "1.18.0", "BSD-3-Clause"},
};

bool beginKeyValueTable(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        return false;
    }
    ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed, 230.0f * ui::theme().dpiScale);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void row(const char* key, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ui::secondaryText("%s", key);
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", value.c_str());
}

std::string seconds(const std::optional<double>& s, const char* missing) {
    if (!s) {
        return missing;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f s", *s);
    return buf;
}

} // namespace

void AboutView::draw(AppContext& ctx) {
    const ui::Theme& t = ui::theme();
    const RuntimeInfo& rt = ctx.runtime;

    ui::beginCard("##build");
    ui::cardTitle("Build");
    if (beginKeyValueTable("##buildtable")) {
        row("Version", std::string("UniForge ") + build::kVersion);
        row("Git commit", build::kGitCommit);
        row("Built", __DATE__ " " __TIME__);
        row("Compiler", build::kCompiler);
        row("Release flags", build::kReleaseFlags);
        row("Runtime", "Static MSVC runtime (/MT): no VC++ redistributable needed");
        ImGui::EndTable();
    }
    ui::endCard();
    ImGui::Dummy(ImVec2(0.0f, t.size.space16));

    ui::beginCard("##runtime");
    ui::cardTitle("This run");
    if (beginKeyValueTable("##runtable")) {
        row("Render backend", rt.render.describe());
        if (rt.render.software()) {
            row("Hardware device", rt.render.hardwareError);
        }
        if (rt.launchMeasured) {
            row("Launch-to-ready (Launch.exe)",
                seconds(rt.launch.fromLauncherSeconds, "n/a: started without Launch.exe"));
            row("Launch-to-ready (UniForge.exe)", seconds(rt.launch.fromProcessSeconds, "n/a"));
        } else {
            row("Launch-to-ready", "measuring…");
        }
        row("Logical CPUs", std::to_string(rt.logicalCpus));
        row("Package folder", platform::pathToUtf8(rt.paths.packageDir));
        row("Logs and settings",
            platform::pathToUtf8(rt.paths.stateDir) +
                (rt.paths.packageWritable ? "" : "  (package folder is read-only: fallback)"));
        ImGui::EndTable();
    }
    if (ui::secondaryButton("Open logs folder")) {
        platform::openInExplorer(rt.paths.logsDir);
    }
    ui::endCard();
    ImGui::Dummy(ImVec2(0.0f, t.size.space16));

    ui::beginCard("##licences");
    ui::cardTitle("Third-party components");
    if (beginKeyValueTable("##licencetable")) {
        for (const Licence& l : kLicences) {
            row(l.name, std::string(l.version) + " · " + l.licence);
        }
        ImGui::EndTable();
    }
    ui::secondaryText("Licence texts: LICENSES/ in the package. Details: DEPENDENCIES.md.");
    ImGui::Dummy(ImVec2(0.0f, t.size.space8));
    ui::secondaryText("Developed with AI assistance (Claude Code); prompts, verification and "
                      "rejected suggestions are recorded in docs/ai_usage_log.md.");
    ui::endCard();
}

} // namespace uniforge::views
