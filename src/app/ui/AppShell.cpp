#include "app/ui/AppShell.hpp"

#include <imgui.h>

#include "app/ui/Theme.hpp"
#include "app/ui/widgets/NavRail.hpp"
#include "app/ui/widgets/StatusBar.hpp"
#include "app/ui/widgets/Widgets.hpp"
#include "app/views/AboutView.hpp"
#include "app/views/BackpropView.hpp"
#include "app/views/GeometryView.hpp"
#include "app/views/HardwareView.hpp"
#include "app/views/OverviewView.hpp"
#include "app/views/PerformanceView.hpp"
#include "app/views/ThermalView.hpp"

namespace uniforge::ui {

namespace {

int index(ViewId id) {
    return static_cast<int>(id);
}

} // namespace

AppShell::AppShell() {
    views_[index(ViewId::Overview)] = std::make_unique<views::OverviewView>();
    views_[index(ViewId::Geometry)] = std::make_unique<views::GeometryView>();
    views_[index(ViewId::Thermal)] = std::make_unique<views::ThermalView>();
    views_[index(ViewId::Performance)] = std::make_unique<views::PerformanceView>();
    views_[index(ViewId::Hardware)] = std::make_unique<views::HardwareView>();
    views_[index(ViewId::Backprop)] = std::make_unique<views::BackpropView>();
    views_[index(ViewId::About)] = std::make_unique<views::AboutView>();
}

void AppShell::draw(AppContext& ctx) {
    const Theme& t = theme();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const ImGuiWindowFlags shellFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking;
    ImGui::Begin("##shell", nullptr, shellFlags);
    ImGui::PopStyleVar();

    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_L, ImGuiInputFlags_RouteGlobal)) {
        log_.toggle();
    }

    // Regions are placed with explicit cursor positions so item spacing never adds gaps.
    const bool compact = vp->WorkSize.x < t.size.compactBreakpoint;
    const float navWidth = compact ? t.size.navCompactWidth : t.size.navWidth;
    const float contentWidth = vp->WorkSize.x - navWidth;
    const float bodyHeight = vp->WorkSize.y - t.size.statusBarHeight;
    const float logHeight = log_.visible() ? t.size.logPanelHeight : 0.0f;

    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    current_ = drawNavRail(current_, compact, navWidth, bodyHeight);

    ImGui::SetCursorPos(ImVec2(navWidth, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(t.size.space24, t.size.space16));
    ImGui::BeginChild("##content", ImVec2(contentWidth, bodyHeight - logHeight),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    views::View& view = *views_[index(current_)];
    viewHeader(view.title(), view.tag(), view.purpose());
    view.draw(ctx);
    if (!log_.visible()) {
        ImGui::Dummy(ImVec2(0.0f, t.size.space16));
        if (secondaryButton("Show log  Ctrl+L")) {
            log_.toggle();
        }
    }
    ImGui::EndChild();

    if (log_.visible()) {
        ImGui::SetCursorPos(ImVec2(navWidth, bodyHeight - logHeight));
        log_.draw(ctx, contentWidth, logHeight);
    }

    ImGui::SetCursorPos(ImVec2(0.0f, bodyHeight));
    drawStatusBar(ctx, vp->WorkSize.x, t.size.statusBarHeight);
    ImGui::End();

    if (ctx.navigateTo) {
        current_ = *ctx.navigateTo;
        ctx.navigateTo.reset();
    }
}

} // namespace uniforge::ui
