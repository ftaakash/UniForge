#pragma once

#include <array>
#include <cstdint>
#include <d3d11.h>
#include <memory>
#include <string>
#include <wrl/client.h>

#include "app/render/RenderMath.hpp"

namespace uniforge {
struct MeshModel;
struct PointModel;
} // namespace uniforge

namespace uniforge::render {

// What to draw this frame. Colours are linear RGBA in [0, 1] (from ui::ThemeViz).
struct SceneParams {
    Mat4 viewProj = Mat4::identity();
    Vec3 lightDir{0.0, 0.0, -1.0}; // direction the light travels
    Vec3 meshOffset{};             // mesh origin − scene origin, m
    Vec3 pointOffset{};            // point-set origin − scene origin, m
    bool showMesh = true;
    bool showPoints = true;
    bool wireframe = false;
    float pointSizePx = 6.0f;
    std::array<float, 4> background{};
    std::array<float, 4> meshFill{};
    std::array<float, 4> meshWire{};
};

// Draws the Geometry viewport into an offscreen texture that the view shows with
// ImGui::Image (spec §7.2):
//   MeshRenderer        flat-shaded triangles, optional wireframe pass with depth bias
//   PointCloudRenderer  one instanced camera-facing quad per node, inferno 1D texture, size in px
// MSAA 4× when the device supports it for the colour and depth formats (WARP does), resolved
// into the texture ImGui samples. All D3D calls happen on the UI thread.
// After a device loss App recreates the device and bumps the generation; the renderer then
// rebuilds its resources and re-uploads the models on the next frame.
class SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    // Creates or re-creates device resources when the device changed. False + error on failure.
    bool prepare(ID3D11Device* device, std::uint64_t deviceGeneration, std::string& error);

    // Uploads models when they differ from the last upload (pointer identity). Null = none.
    void setMesh(const std::shared_ptr<const MeshModel>& mesh);
    void setPoints(const std::shared_ptr<const PointModel>& points);

    // Renders at widthPx × heightPx and returns the texture to show (null on failure).
    ID3D11ShaderResourceView* render(ID3D11DeviceContext* ctx, int widthPx, int heightPx,
                                     const SceneParams& params);

    [[nodiscard]] int msaaSamples() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace uniforge::render
