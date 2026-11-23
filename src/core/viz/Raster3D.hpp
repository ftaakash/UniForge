#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "core/viz/Image.hpp"

namespace uniforge::viz {

// A small CPU rasteriser for the report's renders (build spec §4.1, §12.1: P1, P3–P6, P9,
// P15). It takes plain arrays, not meshes (module rule: viz depends on common only), and is
// deterministic: the same input gives the same pixels on every machine.
//
// Orthographic camera on a view direction (azimuth around z, elevation above the xy plane),
// fitted to a bounding box; z-buffered triangles with two-sided Lambert shading under one
// light fixed to the camera; colour per vertex (interpolated, for fields) or per triangle
// (flat, for materials and labels); line segments (mesh edges, outlines) depth-tested with a
// small bias so edges on a surface stay visible and hidden edges do not. Supersampling with a
// box filter gives antialiased edges.

using Vec3 = std::array<double, 3>;

struct TriangleMesh {
    std::vector<Vec3> positions;
    std::vector<std::array<std::int32_t, 3>> triangles;
    std::vector<Rgb> vertexColor;   // per position (interpolated), or
    std::vector<Rgb> triangleColor; // per triangle (flat); wins when not empty
    bool shaded = true;             // false: colours as given (colour bars, flat diagrams)
};

struct Segment {
    Vec3 a{};
    Vec3 b{};
    Rgb color{0, 0, 0};
};

struct ViewSpec {
    double azimuthDeg = -55.0; // camera direction around +z, from +x
    double elevationDeg = 30.0;
    int width = 1400; // output pixels
    int height = 900;
    int supersample = 3;
    double margin = 0.04; // fraction of the image kept free on every side
    double zoom = 1.0;    // > 1 enlarges (crops) around the centre of the box
    Rgb background{255, 255, 255};
    double ambient = 0.42;  // shading = ambient + (1 − ambient)·|n·l|
    double lineWidth = 1.0; // output pixels
};

class Camera {
public:
    // Fits the box [lo, hi] into the image of `view`.
    [[nodiscard]] static Camera fit(const ViewSpec& view, const Vec3& lo, const Vec3& hi);

    // Output-pixel coordinates (x right, y down) and depth (larger = nearer) of a point.
    [[nodiscard]] Vec3 project(const Vec3& p) const;
    [[nodiscard]] const ViewSpec& view() const noexcept { return view_; }
    [[nodiscard]] Vec3 viewDirection() const noexcept { return d_; } // towards the camera
    [[nodiscard]] Vec3 lightDirection() const noexcept { return light_; }

private:
    ViewSpec view_;
    Vec3 r_{}, u_{}, d_{}, light_{};
    double cx_ = 0.0, cy_ = 0.0, scale_ = 1.0;
};

// Renders meshes and segments; output size is view.width × view.height.
[[nodiscard]] Image render(const Camera& camera, const std::vector<const TriangleMesh*>& meshes,
                           const std::vector<Segment>& segments);

// Bounding box of meshes (and segments), for Camera::fit.
void extendBox(const TriangleMesh& mesh, Vec3& lo, Vec3& hi);
void extendBox(const std::vector<Segment>& segments, Vec3& lo, Vec3& hi);
[[nodiscard]] std::array<Vec3, 2> emptyBox();

} // namespace uniforge::viz
