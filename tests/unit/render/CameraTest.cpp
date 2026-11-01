#include "app/render/Camera.hpp"

#include <cmath>

#include <gtest/gtest.h>

namespace uniforge::render {
namespace {

const Bounds kRod{{-0.5, -0.005, -0.005}, {0.5, 0.005, 0.005}}; // the 1 m × 10 mm × 10 mm bar

TEST(Camera, FitDistancePutsSphereInsideNarrowerFov) {
    const double r = 2.0, fovY = 0.7854;
    // Portrait viewport: the horizontal FOV is narrower and decides the distance.
    const double fovX = 2.0 * std::atan(std::tan(fovY / 2) * 0.5);
    EXPECT_NEAR(Camera::fitDistance(r, fovY, 0.5), r / std::sin(fovX / 2), 1e-12);
    // Landscape: the vertical FOV decides.
    EXPECT_NEAR(Camera::fitDistance(r, fovY, 2.0), r / std::sin(fovY / 2), 1e-12);
}

TEST(Camera, FitCentresTheBoxAndEveryCornerIsOnScreen) {
    Camera cam;
    cam.setViewport(800, 600);
    cam.reset(kRod);
    EXPECT_NEAR(length(cam.target() - kRod.centre()), 0.0, 1e-12);
    const Mat4 vp = cam.viewProjection();
    for (int i = 0; i < 8; ++i) {
        const Vec3 c{(i & 1) ? kRod.max.x : kRod.min.x, (i & 2) ? kRod.max.y : kRod.min.y,
                     (i & 4) ? kRod.max.z : kRod.min.z};
        const ScreenPoint s = projectToScreen(vp, c, 800, 600);
        EXPECT_TRUE(s.visible) << "corner " << i;
        EXPECT_GE(s.x, 0.0);
        EXPECT_LE(s.x, 800.0);
        EXPECT_GE(s.y, 0.0);
        EXPECT_LE(s.y, 600.0);
    }
}

TEST(Camera, TargetProjectsToViewportCentre) {
    Camera cam;
    cam.setViewport(640, 480);
    cam.reset(kRod);
    cam.orbit(57, -23);
    const ScreenPoint s = projectToScreen(cam.viewProjection(), cam.target(), 640, 480);
    EXPECT_NEAR(s.x, 320.0, 1e-6);
    EXPECT_NEAR(s.y, 240.0, 1e-6);
}

TEST(Camera, PitchIsClampedBelowTheUpAxis) {
    Camera cam;
    cam.reset(kRod);
    cam.orbit(0, 1e6);
    EXPECT_LT(cam.pitch(), 1.5708);
    cam.orbit(0, -1e6);
    EXPECT_GT(cam.pitch(), -1.5708);
}

TEST(Camera, PanMovesTheSceneWithTheCursor) {
    Camera cam;
    cam.setViewport(800, 600);
    cam.reset(kRod);
    const Vec3 anchor = cam.target();
    cam.pan(40, 0); // drag 40 px to the right
    const ScreenPoint s = projectToScreen(cam.viewProjection(), anchor, 800, 600);
    EXPECT_NEAR(s.x, 440.0, 1e-6);
    EXPECT_NEAR(s.y, 300.0, 1e-6);
}

TEST(Camera, ZoomKeepsThePointUnderTheCursorFixed) {
    Camera cam;
    cam.setViewport(800, 600);
    cam.reset(kRod);
    // The world point on the target plane under (600, 200): pan the target there and back.
    Camera probe = cam;
    probe.pan(-200, 100); // the point under (600, 200) moves to the centre
    const Vec3 p = probe.target();
    const ScreenPoint before = projectToScreen(cam.viewProjection(), p, 800, 600);
    ASSERT_NEAR(before.x, 600.0, 1e-6);
    ASSERT_NEAR(before.y, 200.0, 1e-6);
    const double d0 = cam.distance();
    cam.zoom(2.0, 600, 200);
    EXPECT_LT(cam.distance(), d0);
    const ScreenPoint after = projectToScreen(cam.viewProjection(), p, 800, 600);
    EXPECT_NEAR(after.x, 600.0, 1e-6);
    EXPECT_NEAR(after.y, 200.0, 1e-6);
}

TEST(Camera, PerspectiveMapsNearAndFarToDepthZeroAndOne) {
    const Mat4 p = perspective(0.7854, 1.0, 0.5, 10.0);
    const auto nearPt = transform(p, Vec3{0, 0, -0.5});
    const auto farPt = transform(p, Vec3{0, 0, -10.0});
    EXPECT_NEAR(nearPt[2] / nearPt[3], 0.0, 1e-12);
    EXPECT_NEAR(farPt[2] / farPt[3], 1.0, 1e-12);
}

} // namespace
} // namespace uniforge::render
