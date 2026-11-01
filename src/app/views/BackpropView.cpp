#include "app/views/BackpropView.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <imgui.h>
#include <implot.h>

#include "app/services/LogSink.hpp"
#include "app/ui/Fonts.hpp"
#include "app/ui/Theme.hpp"
#include "app/ui/widgets/Widgets.hpp"
#include "core/ml/Trainer.hpp"

namespace uniforge::views {

namespace {

// The brief quotes its expected values to 6 decimals (spec §7.6).
constexpr double kBriefTol = 1e-6;
constexpr double kBriefAfterUpdate[ml::kParamCount] = {0.620946, 0.120946, 1.071068, 0.153788};

std::string fmt(const char* format, double v) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, format, v);
    return buffer;
}

// Label + value row inside a two-column key/value table, value in the mono face.
void valueRow(const char* name, double value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ui::secondaryText("%s", name);
    ImGui::TableNextColumn();
    ui::ScopedFont font(ui::fonts().mono, ui::theme().type.mono);
    ImGui::Text("%12.6f", value);
}

bool beginValueTable(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit)) {
        return false;
    }
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed,
                            110.0f * ui::theme().dpiScale);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed);
    return true;
}

// Local stand-in for the shared VerificationBadge (spec §8.4): glyph + word + detail, tooltip
// with the numbers and the tolerance. Integration can swap it for the Phase 4 widget.
void badge(ui::Status status, const char* word, const char* detail, const std::string& tip) {
    ImGui::BeginGroup();
    ui::statusLabel(status, word, detail);
    ImGui::EndGroup();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tip.c_str());
    }
}

} // namespace

BackpropView::BackpropView() = default;

bool BackpropView::isBrief() const {
    const ml::BackpropProblem brief;
    return input_.x == brief.x && input_.y == brief.y && input_.init.w == brief.init.w &&
           input_.init.b == brief.init.b && input_.init.v == brief.init.v &&
           input_.init.c == brief.init.c && input_.lr == brief.lr;
}

void BackpropView::recompute(AppContext& ctx) {
    if (auto r = input_.validate(); !r) {
        inputError_ = r.error();
        return; // keep the last valid result on screen
    }
    inputError_.reset();
    const ml::BackpropProblem& p = input_;
    Computed c;
    c.forward = ml::forward(p.init, p.x, p.y);
    c.check = ml::centralDiff(p.init, p.x, p.y);
    c.next = ml::sgdStep(p.init, c.check.analytic, p.lr);
    c.losses = ml::train(p.init, p.x, p.y, p.lr, p.steps, &c.last);
    c.steps.resize(c.losses.size());
    c.hAt.resize(c.losses.size());
    ml::Params q = p.init;
    for (std::size_t k = 0; k < c.losses.size(); ++k) {
        c.steps[k] = static_cast<double>(k);
        c.hAt[k] = ml::forward(q, p.x, p.y).h;
        q = ml::sgdStep(q, ml::backward(q, p.x, p.y), p.lr);
    }
    c.nonIncreasing = true;
    for (std::size_t k = 1; k < c.losses.size(); ++k) {
        c.nonIncreasing = c.nonIncreasing && c.losses[k] <= c.losses[k - 1];
    }
    ctx.log.debug("backprop: FD " + std::string(c.check.pass ? "pass" : "FAIL") + ", J " +
                  fmt("%.6g", c.losses.front()) + " -> " + fmt("%.3e", c.losses.back()) +
                  " in " + std::to_string(p.steps) + " steps");
    result_ = std::move(c);
}

void BackpropView::draw(AppContext& ctx) {
    const ui::Theme& t = ui::theme();
    if (!result_ && !inputError_) {
        recompute(ctx);
    }
    drawBadges();
    ImGui::Dummy(ImVec2(0.0f, t.size.space8));

    const float inputsW = 330.0f * t.dpiScale;
    ui::beginCard("##bp_inputs", inputsW);
    drawInputs(ctx);
    ui::endCard();
    ImGui::SameLine(0.0f, t.size.space16);
    ImGui::BeginGroup();
    const float half = (ImGui::GetContentRegionAvail().x - t.size.space16) * 0.5f;
    ui::beginCard("##bp_forward", half);
    drawForward();
    ui::endCard();
    ImGui::SameLine(0.0f, t.size.space16);
    ui::beginCard("##bp_update", half);
    drawUpdate();
    ui::endCard();
    ImGui::Dummy(ImVec2(0.0f, t.size.space8));
    ui::beginCard("##bp_grads");
    drawGradients();
    ui::endCard();
    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0.0f, t.size.space16));
    ui::beginCard("##bp_loss");
    drawLoss();
    ui::endCard();
}

void BackpropView::drawBadges() {
    const ui::Theme& t = ui::theme();
    if (!result_) {
        badge(ui::Status::Pending, "FD checks", "no valid input yet", "Fix the inputs first.");
        return;
    }
    const Computed& c = *result_;
    int passed = 0;
    double maxDiff = 0.0;
    for (int i = 0; i < ml::kParamCount; ++i) {
        passed += c.check.passEach[static_cast<std::size_t>(i)] ? 1 : 0;
        maxDiff = std::max(maxDiff, ml::get(c.check.absDiff, i));
    }
    const std::string fdWord = std::to_string(passed) + "/4 FD checks";
    badge(c.check.pass ? ui::Status::Success : ui::Status::Error, fdWord.c_str(),
          "|Δ| ≤ 1e-5",
          "Central differences, ε = 1e-6. Max |analytic − numeric| = " + fmt("%.3e", maxDiff) +
              " (tolerance 1e-5).");

    ImGui::SameLine(0.0f, t.size.space24);
    if (isBrief()) {
        double dev = 0.0;
        for (int i = 0; i < ml::kParamCount; ++i) {
            dev = std::max(dev, std::abs(ml::get(c.next, i) - kBriefAfterUpdate[i]));
        }
        badge(dev <= kBriefTol ? ui::Status::Success : ui::Status::Error,
              "Update matches brief", "≤ 1e-6",
              "Max |after − brief| = " + fmt("%.1e", dev) +
                  " over w, b, v, c (brief: 0.620946, 0.120946, 1.071068, 0.153788).");
    } else {
        badge(ui::Status::Info, "Update vs brief", "n/a — inputs differ from the brief",
              "The brief's expected update applies only to x = 1, y = 2, w = 0.5, b = 0, "
              "v = 1, c = 0, lr = 0.1. Use Reset to brief values.");
    }

    ImGui::SameLine(0.0f, t.size.space24);
    badge(c.nonIncreasing ? ui::Status::Success : ui::Status::Warning,
          c.nonIncreasing ? "Loss non-increasing" : "Loss increases", nullptr,
          "J over " + std::to_string(c.losses.size() - 1) + " steps: " +
              fmt("%.6g", c.losses.front()) + " → " + fmt("%.3e", c.losses.back()) + ".");
}

void BackpropView::drawInputs(AppContext& ctx) {
    const ui::Theme& t = ui::theme();
    ui::cardTitle("Inputs");
    ui::secondaryText("Defaults are the brief's values.");
    ImGui::Dummy(ImVec2(0.0f, t.size.space4));

    bool changed = false;
    const float fieldW = 150.0f * t.dpiScale;
    const auto field = [&](const char* label, double* value) {
        ImGui::SetNextItemWidth(fieldW);
        changed |= ImGui::InputDouble(label, value, 0.0, 0.0, "%.6g");
    };
    field("x  (input)", &input_.x);
    field("y  (target)", &input_.y);
    field("w", &input_.init.w);
    field("b", &input_.init.b);
    field("v", &input_.init.v);
    field("c", &input_.init.c);
    field("lr  (learning rate)", &input_.lr);
    ImGui::SetNextItemWidth(fieldW);
    changed |= ImGui::InputInt("steps", &input_.steps, 10, 100);

    ImGui::Dummy(ImVec2(0.0f, t.size.space4));
    if (ui::secondaryButton("Reset to brief values")) {
        input_ = ml::BackpropProblem{};
        changed = true;
        ctx.log.info("backprop: inputs reset to the brief's values");
    }
    if (changed) {
        recompute(ctx);
    }
    if (inputError_) {
        ImGui::Dummy(ImVec2(0.0f, t.size.space4));
        ImGui::PushTextWrapPos(0.0f);
        ui::statusLabel(ui::Status::Error, "Invalid input");
        ImGui::TextWrapped("%s %s", inputError_->message.c_str(), inputError_->hint.c_str());
        if (result_) {
            ui::secondaryText("Showing the last valid result.");
        }
        ImGui::PopTextWrapPos();
    }
}

void BackpropView::drawForward() {
    ui::cardTitle("Forward pass");
    if (!result_) {
        return;
    }
    const ml::Forward& f = result_->forward;
    if (beginValueTable("##fwd")) {
        valueRow("z = wx + b", f.z);
        valueRow("h = tanh z", f.h);
        valueRow("y_hat = vh + c", f.yhat);
        valueRow("e = y_hat − y", f.err);
        valueRow("J = ½e²", f.loss);
        ImGui::EndTable();
    }
}

void BackpropView::drawUpdate() {
    ui::cardTitle("After one update");
    if (!result_) {
        return;
    }
    ui::secondaryText("new p = p − lr·dJ/dp, all gradients from the original p");
    if (beginValueTable("##upd")) {
        for (int i = 0; i < ml::kParamCount; ++i) {
            valueRow(ml::kParamNames[static_cast<std::size_t>(i)], ml::get(result_->next, i));
        }
        ImGui::EndTable();
    }
}

void BackpropView::drawGradients() {
    const ui::Theme& t = ui::theme();
    ui::cardTitle("Gradients");
    if (!result_) {
        return;
    }
    ui::secondaryText("dc = e · dv = e·h · db = e·v·(1 − h²) · dw = db·x");
    const ml::GradCheck& g = result_->check;
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_SizingStretchSame;
    if (ImGui::BeginTable("##grads", 5, flags)) {
        ImGui::TableSetupColumn("param");
        ImGui::TableSetupColumn("analytic");
        ImGui::TableSetupColumn("FD (ε = 1e-6)");
        ImGui::TableSetupColumn("|diff|");
        ImGui::TableSetupColumn("≤ 1e-5");
        ImGui::TableHeadersRow();
        for (int i = 0; i < ml::kParamCount; ++i) {
            const auto idx = static_cast<std::size_t>(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("dJ/d%s", ml::kParamNames[idx]);
            {
                ui::ScopedFont font(ui::fonts().mono, t.type.mono);
                ImGui::TableNextColumn();
                ImGui::Text("%12.6f", ml::get(g.analytic, i));
                ImGui::TableNextColumn();
                ImGui::Text("%12.6f", ml::get(g.numeric, i));
                ImGui::TableNextColumn();
                ImGui::Text("%10.3e", ml::get(g.absDiff, i));
            }
            ImGui::TableNextColumn();
            if (g.passEach[idx]) {
                ui::statusLabel(ui::Status::Success, "pass");
            } else {
                ui::statusLabel(ui::Status::Error, "fail");
            }
        }
        ImGui::EndTable();
    }
}

void BackpropView::drawLoss() {
    const ui::Theme& t = ui::theme();
    ui::cardTitle("Loss over training");
    if (!result_) {
        return;
    }
    const Computed& c = *result_;
    ImGui::Checkbox("Log scale (y)", &logY_);

    // Measured, not assumed: rate from the last step, tanh slope at the start and the end.
    const std::size_t n = c.losses.size();
    const double h0 = c.hAt.front();
    const double hN = c.hAt.back();
    const double rate = n >= 2 && c.losses[n - 2] > 0.0 ? c.losses[n - 1] / c.losses[n - 2]
                                                        : std::nan("");
    ImGui::PushTextWrapPos(0.0f);
    ui::secondaryText(
        "J falls from %.6f to %.3e in %d steps; at the end each step multiplies J by %.4f "
        "(linear convergence). One sample and four parameters can be fitted exactly, so J "
        "heads to 0.",
        c.losses.front(), c.losses.back(), static_cast<int>(n - 1), rate);
    ui::secondaryText(
        "h = tanh z moves from %.3f to %.3f, so tanh' = 1 − h² drops from %.3f to %.3f: the "
        "gradients of w and b shrink while v and c (not behind tanh) keep most of their step.",
        h0, hN, 1.0 - h0 * h0, 1.0 - hN * hN);
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0.0f, t.size.space4));

    if (ImPlot::BeginPlot("##loss", ImVec2(-1.0f, 340.0f * t.dpiScale))) {
        // Auto-fit follows the toggle and edited inputs; a zero loss is simply not drawn on log.
        ImPlot::SetupAxes("step", "J = ½e²", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        if (logY_) {
            ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
        }
        ImPlotSpec spec;
        spec.LineColor = t.color.accent;
        spec.LineWeight = 2.0f * t.dpiScale;
        ImPlot::PlotLine("J", c.steps.data(), c.losses.data(), static_cast<int>(n), spec);
        ImPlot::EndPlot();
    }
}

} // namespace uniforge::views
