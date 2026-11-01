#pragma once

#include <array>
#include <filesystem>
#include <optional>

#include "app/ui/widgets/DataViews.hpp"
#include "app/views/View.hpp"
#include "core/thermal/RodProblem.hpp"

namespace uniforge {
struct ThermalStudyResult;
}

namespace uniforge::views {

// Task C (spec §8.6 Thermal): Form → Run → Result. Parameters with validation and brief
// defaults, mesh selection, convergence study on the job thread, ImPlot tabs (T vs x with the
// exact solution dashed on top, error vs h log–log with a slope-2 guide, midpoint errors),
// convergence table, CSV/PNG export and verification badges with tolerances.
class ThermalView final : public View {
public:
    [[nodiscard]] const char* title() const override { return "Thermal Analysis"; }
    [[nodiscard]] const char* tag() const override { return "Task C"; }
    [[nodiscard]] const char* purpose() const override {
        return "Steady 1D heat conduction in a rod with two-node linear elements, checked against "
               "the exact solution.";
    }
    void draw(AppContext& ctx) override;

private:
    void drawParams(AppContext& ctx);
    void drawPlots(AppContext& ctx, const ThermalStudyResult* r, bool stale);
    void drawTable(AppContext& ctx, const ThermalStudyResult* r);
    void exportChoice(AppContext& ctx, const ThermalStudyResult& r, int choice);

    thermal::RodProblem problem_{};
    std::array<bool, 4> meshes_{true, true, true, true}; // N = 10, 20, 40, 80
    std::string invalidReason_;
    ui::ExportStatus exportStatus_;
    ui::ScreenRect plotRect_;
    std::optional<std::filesystem::path> pngPending_; // captured on the next frame
    int activeTab_ = 0;
};

} // namespace uniforge::views
