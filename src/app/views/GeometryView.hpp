#pragma once

#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>

#include "app/ui/widgets/Viewport3D.hpp"
#include "app/views/View.hpp"
#include "core/common/Error.hpp"

namespace uniforge::views {

// Task B (spec §8.6 Geometry): STL + nodal-temperature CSV import through JobRunner, 3D
// viewport with legend, NodeInspector and verification badges. A failed or cancelled import
// keeps the previous model (ResultStore slots are only replaced on success).
class GeometryView final : public View {
public:
    [[nodiscard]] const char* title() const override { return "Geometry"; }
    [[nodiscard]] const char* tag() const override { return "Task B"; }
    [[nodiscard]] const char* purpose() const override {
        return "Import ASCII STL geometry and nodal-temperature CSV results and inspect them in "
               "3D.";
    }
    void draw(AppContext& ctx) override;

    enum class Kind { Stl, Csv };

    // Locates data/ next to the package (or above a dev build) for "Load samples".
    [[nodiscard]] static std::filesystem::path findDataDir(const std::filesystem::path& start);

private:
    struct Import {
        Kind kind;
        std::filesystem::path file;
    };
    // Error of the last failed import, written by the job thread before the job finishes.
    struct Failure {
        std::mutex mutex;
        std::optional<Error> error;
        Kind kind = Kind::Stl;
    };

    void drawControls(AppContext& ctx);
    void drawSummary(AppContext& ctx);
    void queueImport(Kind kind, std::filesystem::path file);
    void pumpQueue(AppContext& ctx);
    void browse(AppContext& ctx, Kind kind);

    ui::Viewport3D viewport_;
    std::deque<Import> queue_;
    std::shared_ptr<Failure> failure_ = std::make_shared<Failure>();
    std::optional<Error> shownError_;
    Kind shownErrorKind_ = Kind::Stl;
    std::uint64_t lastFailedJob_ = 0;
    std::filesystem::path pendingStl_, pendingCsv_; // what the running/queued import loads
    bool showMesh_ = true;
    bool showPoints_ = true;
    float pointSize_ = 7.0f;
};

} // namespace uniforge::views
