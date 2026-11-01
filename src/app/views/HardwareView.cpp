#include "app/views/HardwareView.hpp"

#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

#include <imgui.h>

#include "app/platform/Paths.hpp"
#include "app/services/DeviceManager.hpp"
#include "app/services/LogSink.hpp"
#include "app/ui/Theme.hpp"
#include "app/ui/widgets/Widgets.hpp"

namespace uniforge::views {

namespace {

using Cmd = DeviceManager::Command;

constexpr const char* kBanner =
    "All devices on this page are software simulations (Task E). No hardware is accessed.";

ui::Status statusFor(hw::DeviceState s) {
    switch (s) {
    case hw::DeviceState::Connected:
        return ui::Status::Success;
    case hw::DeviceState::Faulted:
        return ui::Status::Error;
    case hw::DeviceState::Connecting:
    case hw::DeviceState::Recovering:
        return ui::Status::Warning;
    case hw::DeviceState::Disconnected:
        break;
    }
    return ui::Status::Pending;
}

std::string formatValue(const hw::Telemetry& t) {
    char buf[64];
    if (t.channel == "idcode") {
        std::snprintf(buf, sizeof buf, "0x%08X",
                      static_cast<unsigned>(static_cast<std::uint32_t>(t.value)));
    } else if (t.channel == "done") {
        std::snprintf(buf, sizeof buf, "%s", t.value != 0.0 ? "1 (configured)" : "0");
    } else if (t.unit == "V") {
        std::snprintf(buf, sizeof buf, "%.3f V", t.value);
    } else {
        std::snprintf(buf, sizeof buf, "%.2f %s", t.value, t.unit.c_str());
    }
    return buf;
}

// The package keeps docs/ next to app/; a developer build runs from build/<preset>/bin/<cfg>,
// so walk up a few folders to find the source tree's docs/.
std::filesystem::path findDesignNotes(const std::filesystem::path& start) {
    std::filesystem::path dir = start;
    for (int i = 0; i < 6 && !dir.empty(); ++i) {
        const std::filesystem::path candidate = dir / "docs" / "embedded_design.md";
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
        if (dir == dir.parent_path()) {
            break;
        }
        dir = dir.parent_path();
    }
    return {};
}

} // namespace

HardwareView::HardwareView() = default;
HardwareView::~HardwareView() = default;

void HardwareView::draw(AppContext& ctx) {
    const ui::Theme& t = ui::theme();
    if (!devices_) {
        devices_ = std::make_unique<DeviceManager>(&ctx.log);
    }
    const HardwareSnapshot snap = devices_->snapshot();
    const bool busy = devices_->busy();

    // Simulated banner (spec §8.5): glyph + word + colour, never colour-only.
    ui::beginCard("##hwbanner");
    ui::chip("◇ SIMULATED", t.color.simulated);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextWrapped("%s", kBanner);
    ui::endCard();
    ImGui::Dummy(ImVec2(0.0f, t.size.space8));

    if (ui::primaryButton(snap.discovered ? "Discover again" : "Discover")) {
        devices_->post(Cmd::Discover);
    }
    ImGui::SameLine();
    if (ui::secondaryButton("Design notes →")) {
        openDesignNotes(ctx);
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (busy) {
        ui::statusLabel(ui::Status::Info, "Working", "waiting for a simulated reply...");
    } else {
        ui::secondaryText("Telemetry is read at 10 Hz from connected devices. Timeout 250 ms, "
                          "3 retries at 100/200/400 ms.");
    }
    ImGui::Dummy(ImVec2(0.0f, t.size.space8));

    if (!snap.discovered) {
        ui::beginCard("##hwempty");
        ui::emptyState("No devices discovered",
                       "Press Discover to list the three simulated devices: an STM32 Nucleo "
                       "microcontroller board, a Basys 3 FPGA board and a J-Link debug probe.");
        ui::endCard();
        return;
    }

    const int n = static_cast<int>(snap.devices.size());
    const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX;
    if (ImGui::BeginTable("##devices", n, flags)) {
        ImGui::TableNextRow();
        for (int i = 0; i < n; ++i) {
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            drawDeviceCard(i, snap.devices[static_cast<std::size_t>(i)], busy);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void HardwareView::drawDeviceCard(int index, const DeviceSnapshot& d, bool busy) {
    const ui::Theme& t = ui::theme();
    const hw::DeviceState s = d.state;
    ui::beginCard("##device");

    ui::chip(std::string(hw::toString(d.info.kind)).c_str(), t.color.accent);
    ImGui::SameLine();
    if (d.info.simulated) {
        ui::chip("◇ SIMULATED", t.color.simulated);
    }
    ui::cardTitle(d.info.model.c_str());
    ui::secondaryText("%s", d.info.id.c_str());
    ImGui::PushTextWrapPos(0.0f);
    ui::secondaryText("%s", d.info.transport.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0.0f, t.size.space4));
    ui::statusLabel(statusFor(s), std::string(hw::toString(s)).c_str());

    ImGui::SeparatorText("Capabilities");
    for (const hw::Capability& c : d.capabilities) {
        ImGui::BulletText("%s", c.name.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", c.description.c_str());
        }
    }

    ImGui::SeparatorText("Telemetry (simulated)");
    if (d.latest.empty()) {
        ui::secondaryText("No telemetry yet. Connect to start reading.");
    } else {
        for (std::size_t c = 0; c < d.latest.size(); ++c) {
            const hw::Telemetry& tm = d.latest[c];
            ImGui::Text("%s", tm.channel.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.45f);
            ImGui::TextUnformatted(formatValue(tm).c_str());
            const bool numeric = tm.channel != "idcode" && tm.channel != "done";
            if (numeric && c < d.history.size() && d.history[c].size() > 1) {
                const std::vector<float>& h = d.history[c];
                ImGui::PlotLines("##spark", h.data(), static_cast<int>(h.size()), 0, nullptr,
                                 FLT_MAX, FLT_MAX,
                                 ImVec2(ImGui::GetContentRegionAvail().x, 28.0f * t.dpiScale));
            }
        }
    }

    ImGui::SeparatorText("Actions");
    const bool connected = s == hw::DeviceState::Connected;
    const bool faulted = s == hw::DeviceState::Faulted;
    const bool disconnected = s == hw::DeviceState::Disconnected;
    if (connected || faulted) {
        if (ui::secondaryButton("Disconnect", !busy, "Waiting for the device thread")) {
            devices_->post(Cmd::Disconnect, index);
        }
    } else {
        if (ui::primaryButton("Connect", disconnected && !busy,
                              busy ? "Waiting for the device thread" : "Already connecting")) {
            devices_->post(Cmd::Connect, index);
        }
    }
    ImGui::SameLine();
    if (ui::secondaryButton("Inject timeout", connected,
                            "Connect first: the next request will time out once")) {
        devices_->post(Cmd::InjectTimeout, index);
    }
    if (ui::secondaryButton("Retry", faulted && !busy, "Only available after a fault")) {
        devices_->post(Cmd::Retry, index);
    }
    ImGui::SameLine();
    if (ui::secondaryButton("Reset fault", faulted && !busy, "Only available after a fault")) {
        devices_->post(Cmd::ResetFault, index);
    }

    if (d.lastError) {
        ImGui::Dummy(ImVec2(0.0f, t.size.space4));
        ImGui::PushTextWrapPos(0.0f);
        ui::statusLabel(ui::Status::Error, "Fault", nullptr);
        ImGui::TextUnformatted(d.lastError->message.c_str());
        ui::secondaryText("%s", d.lastError->hint.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!d.recoverySummary.empty()) {
        ImGui::SeparatorText("Last recovery");
        ui::statusLabel(d.lastError ? ui::Status::Error : ui::Status::Success,
                        d.recoverySummary.c_str());
        for (const hw::RetryAttempt& a : d.retryTrail) {
            ui::secondaryText("retry %d after %lld ms: %s", a.retry,
                              static_cast<long long>(a.backoff.count()), a.detail.c_str());
        }
    }
    ui::endCard();
}

void HardwareView::openDesignNotes(AppContext& ctx) {
    std::filesystem::path notes = findDesignNotes(ctx.runtime.paths.packageDir);
    if (notes.empty()) {
        notes = findDesignNotes(ctx.runtime.paths.exeDir);
    }
    if (notes.empty()) {
        ctx.log.warn("docs/embedded_design.md not found next to the application. Open it from "
                     "the docs folder of the package.");
        return;
    }
    ctx.log.info("opening docs/embedded_design.md");
    platform::openInExplorer(notes);
}

} // namespace uniforge::views
