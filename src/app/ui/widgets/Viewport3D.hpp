#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include <imgui.h>

#include "app/AppContext.hpp"
#include "app/render/Camera.hpp"
#include "app/render/SceneRenderer.hpp"
#include "app/services/GeometryModel.hpp"

namespace uniforge::ui {

struct ViewportScene {
    std::shared_ptr<const MeshModel> mesh;
    std::shared_ptr<const PointModel> points;
    bool showMesh = true;
    bool showPoints = true;
    float pointSizePx = 6.0f;
};

// 3D viewport (spec §8.4 Viewport3D, §7.2): toolbar (Fit F · Reset R · Wireframe W · Axes),
// offscreen D3D11 image, orbit/pan/zoom input, axes triad, colour legend on the right edge,
// bbox dimensions bottom-right, pick ring. A click (no drag) on a point selects the nearest
// node within 6 px.
class Viewport3D {
public:
    void draw(AppContext& ctx, const ViewportScene& scene, ImVec2 size);

    // Index into scene.points->source.nodes of the selected node, if any.
    [[nodiscard]] std::optional<std::size_t> picked() const { return picked_; }
    [[nodiscard]] int msaaSamples() const { return renderer_.msaaSamples(); }

private:
    void handleInput(const ViewportScene& scene, const render::Bounds& bounds, ImVec2 origin);
    void drawOverlays(const ViewportScene& scene, ImVec2 origin, ImVec2 size);
    void pick(const ViewportScene& scene, double x, double y);

    render::Camera camera_;
    render::SceneRenderer renderer_;
    std::string renderError_;
    const MeshModel* lastMesh_ = nullptr;
    const PointModel* lastPoints_ = nullptr;
    render::Vec3 meshOffset_{}, pointOffset_{};
    render::Bounds bounds_{};
    std::optional<std::size_t> picked_;
    bool wireframe_ = false;
    bool axes_ = true;
    bool fitPending_ = true;
};

} // namespace uniforge::ui
