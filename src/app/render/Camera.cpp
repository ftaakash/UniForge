#include "app/render/Camera.hpp"

#include <algorithm>

namespace uniforge::render {

namespace {

constexpr double kMaxPitch = 1.553; // 89°: never look straight along the up axis
constexpr double kOrbitRadPerPx = 0.008;
constexpr double kZoomPerStep = 0.85; // distance factor per wheel notch
constexpr Vec3 kUp{0.0, 0.0, 1.0};

} // namespace

Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 f = normalize(target - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 r = Mat4::identity();
    r.m[0] = {s.x, s.y, s.z, -dot(s, eye)};
    r.m[1] = {u.x, u.y, u.z, -dot(u, eye)};
    r.m[2] = {-f.x, -f.y, -f.z, dot(f, eye)};
    return r;
}

Mat4 perspective(double fovY, double aspect, double zNear, double zFar) {
    const double yScale = 1.0 / std::tan(fovY * 0.5);
    Mat4 r;
    r.m[0][0] = yScale / aspect;
    r.m[1][1] = yScale;
    r.m[2][2] = zFar / (zNear - zFar);
    r.m[2][3] = zNear * zFar / (zNear - zFar);
    r.m[3][2] = -1.0;
    return r;
}

ScreenPoint projectToScreen(const Mat4& viewProj, Vec3 p, double width, double height) {
    const auto c = transform(viewProj, p);
    ScreenPoint s;
    if (c[3] <= 0.0) {
        return s; // behind the camera
    }
    const double nx = c[0] / c[3];
    const double ny = c[1] / c[3];
    s.depth = c[2] / c[3];
    s.x = (nx * 0.5 + 0.5) * width;
    s.y = (0.5 - ny * 0.5) * height;
    s.visible = s.depth >= 0.0 && s.depth <= 1.0;
    return s;
}

double Camera::fitDistance(double radius, double fovY, double aspect) {
    const double fovX = 2.0 * std::atan(std::tan(fovY * 0.5) * aspect);
    const double half = 0.5 * std::min(fovY, fovX);
    return radius / std::sin(half);
}

void Camera::setViewport(double widthPx, double heightPx) {
    width_ = std::max(1.0, widthPx);
    height_ = std::max(1.0, heightPx);
}

void Camera::fit(const Bounds& scene) {
    sceneRadius_ = std::max(scene.radius(), 1e-9);
    target_ = scene.centre();
    distance_ = fitDistance(sceneRadius_, kFovY, aspect()) * kFitMargin;
}

void Camera::reset(const Bounds& scene) {
    yaw_ = kDefaultYaw;
    pitch_ = kDefaultPitch;
    fit(scene);
}

void Camera::basis(Vec3& forward, Vec3& right, Vec3& up) const {
    forward = normalize(target_ - eye());
    right = normalize(cross(forward, kUp));
    up = cross(right, forward);
}

Vec3 Camera::eye() const {
    const double cp = std::cos(pitch_);
    const Vec3 dir{cp * std::cos(yaw_), cp * std::sin(yaw_), std::sin(pitch_)};
    return target_ + dir * distance_;
}

void Camera::orbit(double dxPx, double dyPx) {
    yaw_ -= dxPx * kOrbitRadPerPx;
    pitch_ = std::clamp(pitch_ + dyPx * kOrbitRadPerPx, -kMaxPitch, kMaxPitch);
}

double Camera::worldPerPixel() const {
    return 2.0 * distance_ * std::tan(kFovY * 0.5) / height_;
}

void Camera::pan(double dxPx, double dyPx) {
    Vec3 f, r, u;
    basis(f, r, u);
    const double k = worldPerPixel();
    // Dragging right moves the scene right, so the target moves left.
    target_ = target_ - r * (dxPx * k) + u * (dyPx * k);
}

void Camera::zoom(double steps, double cursorX, double cursorY) {
    const double factor = std::pow(kZoomPerStep, steps);
    const double newDistance =
        std::clamp(distance_ * factor, sceneRadius_ * 1e-3, sceneRadius_ * 1e3);
    const double applied = newDistance / distance_;

    // The point under the cursor on the plane through the target stays under the cursor:
    // its offset from the target scales with the distance.
    Vec3 f, r, u;
    basis(f, r, u);
    const double k = worldPerPixel();
    const Vec3 p =
        target_ + r * ((cursorX - width_ * 0.5) * k) - u * ((cursorY - height_ * 0.5) * k);
    target_ = p + (target_ - p) * applied;
    distance_ = newDistance;
}

Mat4 Camera::view() const {
    return lookAt(eye(), target_, kUp);
}

Mat4 Camera::projection() const {
    // Depth range sized to the scene around the current target so near-plane clipping does
    // not cut the model while zoomed in; 1e-4 relative floor keeps the depth buffer usable.
    const double reach = sceneRadius_ * 2.0 + length(target_);
    const double zNear = std::max(distance_ - reach, distance_ * 1e-3);
    const double zFar = distance_ + reach;
    return perspective(kFovY, aspect(), zNear, std::max(zFar, zNear * 1.01));
}

} // namespace uniforge::render
