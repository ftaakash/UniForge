#pragma once

#include <array>
#include <cmath>

// Minimal 3D maths for the viewer: double-precision vectors and 4×4 matrices. Matrices are
// row-major and multiply column vectors (clip = M · v); the shaders declare them `row_major`
// and use mul(M, v), so the same layout goes to the GPU unchanged.
namespace uniforge::render {

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

inline Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 operator*(Vec3 a, double s) {
    return {a.x * s, a.y * s, a.z * s};
}
inline double dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(Vec3 a) {
    return std::sqrt(dot(a, a));
}
inline Vec3 normalize(Vec3 a) {
    const double l = length(a);
    return l > 0.0 ? a * (1.0 / l) : Vec3{};
}

struct Mat4 {
    std::array<std::array<double, 4>, 4> m{}; // m[row][col]

    static Mat4 identity() {
        Mat4 r;
        for (int i = 0; i < 4; ++i) {
            r.m[i][i] = 1.0;
        }
        return r;
    }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            double s = 0.0;
            for (int k = 0; k < 4; ++k) {
                s += a.m[i][k] * b.m[k][j];
            }
            r.m[i][j] = s;
        }
    }
    return r;
}

// Homogeneous transform of a point: returns (x, y, z, w) before the perspective divide.
inline std::array<double, 4> transform(const Mat4& a, Vec3 p) {
    std::array<double, 4> r{};
    for (int i = 0; i < 4; ++i) {
        r[static_cast<std::size_t>(i)] =
            a.m[i][0] * p.x + a.m[i][1] * p.y + a.m[i][2] * p.z + a.m[i][3];
    }
    return r;
}

// Right-handed view matrix (camera looks down −z in view space).
Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up);
// Right-handed perspective with Direct3D depth range [0, 1].
Mat4 perspective(double fovY, double aspect, double zNear, double zFar);

// Screen position in pixels (origin top-left, y down) of a world point, and its NDC depth.
struct ScreenPoint {
    double x = 0.0, y = 0.0, depth = 0.0;
    bool visible = false; // in front of the camera and inside the depth range
};
ScreenPoint projectToScreen(const Mat4& viewProj, Vec3 p, double width, double height);

} // namespace uniforge::render
