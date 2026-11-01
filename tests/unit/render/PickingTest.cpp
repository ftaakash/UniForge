#include "app/render/Picking.hpp"

#include <gtest/gtest.h>

#include "app/render/Camera.hpp"

namespace uniforge::render {
namespace {

struct Scene {
    Camera cam;
    Mat4 vp;
    Scene() {
        cam.setViewport(800, 600);
        cam.reset(Bounds{{-1, -1, -1}, {1, 1, 1}});
        vp = cam.viewProjection();
    }
};

std::vector<float> cloud(std::initializer_list<Vec3> pts) {
    std::vector<float> xyz;
    for (const Vec3& p : pts) {
        xyz.push_back(static_cast<float>(p.x));
        xyz.push_back(static_cast<float>(p.y));
        xyz.push_back(static_cast<float>(p.z));
    }
    return xyz;
}

TEST(Picking, ReturnsNothingForAnEmptyCloud) {
    Scene s;
    EXPECT_FALSE(pickNearest({}, s.vp, 800, 600, 400, 300).has_value());
}

TEST(Picking, PicksThePointUnderTheCursor) {
    Scene s;
    const auto hit = pickNearest(cloud({{0, 0, 0}, {0.8, 0, 0}}), s.vp, 800, 600, 400, 300);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->index, 0u);
    EXPECT_NEAR(hit->distancePx, 0.0, 1e-4);
}

TEST(Picking, RespectsTheSixPixelRadius) {
    Scene s;
    const auto xyz = cloud({{0, 0, 0}});
    EXPECT_TRUE(pickNearest(xyz, s.vp, 800, 600, 405.9, 300).has_value());
    EXPECT_FALSE(pickNearest(xyz, s.vp, 800, 600, 406.1, 300).has_value());
}

TEST(Picking, ChoosesTheNearestOnScreen) {
    Scene s;
    const ScreenPoint p1 = projectToScreen(s.vp, Vec3{0.8, 0, 0}, 800, 600);
    const auto hit = pickNearest(cloud({{0, 0, 0}, {0.8, 0, 0}}), s.vp, 800, 600, p1.x + 1.0, p1.y);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->index, 1u);
}

TEST(Picking, OnATiePrefersThePointNearerTheCamera) {
    Scene s;
    // Two points on the line of sight through the target project to the same pixel.
    const Vec3 dir = normalize(s.cam.target() - s.cam.eye());
    const Vec3 nearP = s.cam.target() - dir * 0.5;
    const Vec3 farP = s.cam.target() + dir * 0.5;
    const auto hit = pickNearest(cloud({farP, nearP}), s.vp, 800, 600, 400, 300);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->index, 1u);
}

TEST(Picking, IgnoresPointsBehindTheCamera) {
    Scene s;
    const Vec3 behind = s.cam.eye() + (s.cam.eye() - s.cam.target());
    EXPECT_FALSE(pickNearest(cloud({behind}), s.vp, 800, 600, 400, 300).has_value());
}

} // namespace
} // namespace uniforge::render
