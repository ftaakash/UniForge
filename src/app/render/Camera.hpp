#pragma once

#include "app/render/RenderMath.hpp"

namespace uniforge::render {

// Axis-aligned box in scene coordinates (metres, already recentred).
struct Bounds {
    Vec3 min{}, max{};
    [[nodiscard]] Vec3 centre() const { return (min + max) * 0.5; }
    [[nodiscard]] double radius() const { return length(max - min) * 0.5; } // bounding sphere
};

// Orbit camera for the Geometry viewport (spec §7.2), z-up like CAD tools.
//   orbit  LMB drag      yaw/pitch around the target
//   pan    MMB / Shift+LMB drag   moves the target in the view plane, 1:1 with the cursor
//   zoom   wheel         towards the point under the cursor
//   fit    F             the bounding sphere fills the narrower field of view
//   reset  R             default direction, then fit
// Pure maths, no GPU: tested in tests/unit/render/CameraTest.cpp.
class Camera {
public:
    static constexpr double kDefaultYaw = -0.9;   // rad, looking from −y, +x to the right
    static constexpr double kDefaultPitch = 0.45; // rad above the horizon
    static constexpr double kFovY = 0.7854;       // 45°
    static constexpr double kFitMargin = 1.05;    // 5 % space around the sphere

    void setViewport(double widthPx, double heightPx);
    void fit(const Bounds& scene);
    void reset(const Bounds& scene);

    void orbit(double dxPx, double dyPx);
    void pan(double dxPx, double dyPx);
    // steps > 0 zooms in; (cursorX, cursorY) in viewport pixels.
    void zoom(double steps, double cursorX, double cursorY);

    [[nodiscard]] Vec3 eye() const;
    [[nodiscard]] Vec3 target() const { return target_; }
    [[nodiscard]] double distance() const { return distance_; }
    [[nodiscard]] double yaw() const { return yaw_; }
    [[nodiscard]] double pitch() const { return pitch_; }
    [[nodiscard]] double aspect() const { return width_ / height_; }
    [[nodiscard]] double width() const { return width_; }
    [[nodiscard]] double height() const { return height_; }

    [[nodiscard]] Mat4 view() const;
    [[nodiscard]] Mat4 projection() const;
    [[nodiscard]] Mat4 viewProjection() const { return projection() * view(); }

    // Distance that makes a sphere of `radius` fill the narrower field of view.
    [[nodiscard]] static double fitDistance(double radius, double fovY, double aspect);

private:
    // World size of one pixel at the target's depth.
    [[nodiscard]] double worldPerPixel() const;
    void basis(Vec3& forward, Vec3& right, Vec3& up) const;

    Vec3 target_{};
    double distance_ = 3.0;
    double yaw_ = kDefaultYaw;
    double pitch_ = kDefaultPitch;
    double width_ = 800.0;
    double height_ = 600.0;
    double sceneRadius_ = 1.0;
};

} // namespace uniforge::render
