#include "core/viz/Raster3D.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace uniforge::viz {

namespace {

constexpr double kPi = 3.14159265358979323846;

Vec3 sub(const Vec3& a, const Vec3& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
double dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec3 normalized(const Vec3& a) {
    const double n = std::sqrt(dot(a, a));
    return n > 0.0 ? Vec3{a[0] / n, a[1] / n, a[2] / n} : Vec3{0.0, 0.0, 0.0};
}

std::uint8_t channel(double v) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 255.0)));
}

struct Target {
    Image color;
    std::vector<double> depth;
    int w = 0, h = 0;
};

} // namespace

std::array<Vec3, 2> emptyBox() {
    const double inf = std::numeric_limits<double>::infinity();
    return {Vec3{inf, inf, inf}, Vec3{-inf, -inf, -inf}};
}

void extendBox(const TriangleMesh& mesh, Vec3& lo, Vec3& hi) {
    for (const Vec3& p : mesh.positions) {
        for (std::size_t k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], p[k]);
            hi[k] = std::max(hi[k], p[k]);
        }
    }
}

void extendBox(const std::vector<Segment>& segments, Vec3& lo, Vec3& hi) {
    for (const Segment& s : segments) {
        for (const Vec3* p : {&s.a, &s.b}) {
            for (std::size_t k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], (*p)[k]);
                hi[k] = std::max(hi[k], (*p)[k]);
            }
        }
    }
}

Camera Camera::fit(const ViewSpec& view, const Vec3& lo, const Vec3& hi) {
    Camera c;
    c.view_ = view;
    const double az = view.azimuthDeg * kPi / 180.0;
    const double el = view.elevationDeg * kPi / 180.0;
    c.d_ = {std::cos(el) * std::cos(az), std::cos(el) * std::sin(az), std::sin(el)};
    c.r_ = {-std::sin(az), std::cos(az), 0.0};
    c.u_ = cross(c.d_, c.r_);
    // Light from the upper left, slightly in front of the object.
    c.light_ = normalized({-0.45 * c.r_[0] + 0.6 * c.u_[0] + 0.66 * c.d_[0],
                           -0.45 * c.r_[1] + 0.6 * c.u_[1] + 0.66 * c.d_[1],
                           -0.45 * c.r_[2] + 0.6 * c.u_[2] + 0.66 * c.d_[2]});
    double xMin = std::numeric_limits<double>::infinity(), xMax = -xMin;
    double yMin = xMin, yMax = -xMin;
    for (int corner = 0; corner < 8; ++corner) {
        const Vec3 p{(corner & 1) ? hi[0] : lo[0], (corner & 2) ? hi[1] : lo[1],
                     (corner & 4) ? hi[2] : lo[2]};
        const double sx = dot(p, c.r_);
        const double sy = dot(p, c.u_);
        xMin = std::min(xMin, sx);
        xMax = std::max(xMax, sx);
        yMin = std::min(yMin, sy);
        yMax = std::max(yMax, sy);
    }
    c.cx_ = 0.5 * (xMin + xMax);
    c.cy_ = 0.5 * (yMin + yMax);
    const double usableW = view.width * (1.0 - 2.0 * view.margin);
    const double usableH = view.height * (1.0 - 2.0 * view.margin);
    const double dx = std::max(xMax - xMin, 1e-30);
    const double dy = std::max(yMax - yMin, 1e-30);
    c.scale_ = std::min(usableW / dx, usableH / dy) * view.zoom;
    return c;
}

Vec3 Camera::project(const Vec3& p) const {
    return {0.5 * view_.width + (dot(p, r_) - cx_) * scale_,
            0.5 * view_.height - (dot(p, u_) - cy_) * scale_, dot(p, d_) * scale_};
}

Image render(const Camera& camera, const std::vector<const TriangleMesh*>& meshes,
             const std::vector<Segment>& segments) {
    const ViewSpec& v = camera.view();
    const int ss = std::max(1, v.supersample);
    Target t;
    t.w = v.width * ss;
    t.h = v.height * ss;
    t.color = Image(t.w, t.h, v.background);
    t.depth.assign(static_cast<std::size_t>(t.w) * static_cast<std::size_t>(t.h),
                   -std::numeric_limits<double>::infinity());
    const Vec3 light = camera.lightDirection();
    const auto toTarget = [&](const Vec3& p) {
        const Vec3 q = camera.project(p);
        return Vec3{q[0] * ss, q[1] * ss, q[2]};
    };

    double depthLo = std::numeric_limits<double>::infinity(), depthHi = -depthLo;
    for (const TriangleMesh* mesh : meshes) {
        const bool flat = !mesh->triangleColor.empty();
        for (std::size_t tri = 0; tri < mesh->triangles.size(); ++tri) {
            const auto& ids = mesh->triangles[tri];
            const Vec3& p0 = mesh->positions[static_cast<std::size_t>(ids[0])];
            const Vec3& p1 = mesh->positions[static_cast<std::size_t>(ids[1])];
            const Vec3& p2 = mesh->positions[static_cast<std::size_t>(ids[2])];
            const Vec3 n = normalized(cross(sub(p1, p0), sub(p2, p0)));
            const double shade =
                mesh->shaded ? v.ambient + (1.0 - v.ambient) * std::fabs(dot(n, light)) : 1.0;
            const Vec3 a = toTarget(p0), b = toTarget(p1), c = toTarget(p2);
            depthLo = std::min({depthLo, a[2], b[2], c[2]});
            depthHi = std::max({depthHi, a[2], b[2], c[2]});
            const double area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
            if (std::fabs(area) < 1e-12) {
                continue;
            }
            Rgb c0, c1, c2;
            if (flat) {
                c0 = c1 = c2 = mesh->triangleColor[tri];
            } else if (!mesh->vertexColor.empty()) {
                c0 = mesh->vertexColor[static_cast<std::size_t>(ids[0])];
                c1 = mesh->vertexColor[static_cast<std::size_t>(ids[1])];
                c2 = mesh->vertexColor[static_cast<std::size_t>(ids[2])];
            } else {
                c0 = c1 = c2 = Rgb{180, 180, 180};
            }
            const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a[0], b[0], c[0]}))));
            const int x1 =
                std::min(t.w - 1, static_cast<int>(std::ceil(std::max({a[0], b[0], c[0]}))));
            const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a[1], b[1], c[1]}))));
            const int y1 =
                std::min(t.h - 1, static_cast<int>(std::ceil(std::max({a[1], b[1], c[1]}))));
            for (int y = y0; y <= y1; ++y) {
                const double py = y + 0.5;
                for (int x = x0; x <= x1; ++x) {
                    const double px = x + 0.5;
                    const double w0 =
                        ((b[0] - px) * (c[1] - py) - (b[1] - py) * (c[0] - px)) / area;
                    const double w1 =
                        ((c[0] - px) * (a[1] - py) - (c[1] - py) * (a[0] - px)) / area;
                    const double w2 = 1.0 - w0 - w1;
                    if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) {
                        continue;
                    }
                    const double z = w0 * a[2] + w1 * b[2] + w2 * c[2];
                    const std::size_t at =
                        static_cast<std::size_t>(y) * static_cast<std::size_t>(t.w) +
                        static_cast<std::size_t>(x);
                    if (z <= t.depth[at]) {
                        continue;
                    }
                    t.depth[at] = z;
                    Rgb out;
                    for (std::size_t k = 0; k < 3; ++k) {
                        out[k] = channel(shade * (w0 * c0[k] + w1 * c1[k] + w2 * c2[k]));
                    }
                    t.color.set(x, y, out);
                }
            }
        }
    }

    // Segments: a square brush stepped along the line, depth-tested with a bias of 0.5 % of
    // the depth range (so edges lying on a visible surface win against it).
    for (const Segment& s : segments) {
        const Vec3 a = toTarget(s.a), b = toTarget(s.b);
        depthLo = std::min({depthLo, a[2], b[2]});
        depthHi = std::max({depthHi, a[2], b[2]});
    }
    const double bias = std::isfinite(depthHi - depthLo) ? 0.005 * (depthHi - depthLo) + 1e-9 : 0.0;
    const int brush = std::max(1, static_cast<int>(std::lround(v.lineWidth * ss)));
    for (const Segment& s : segments) {
        const Vec3 a = toTarget(s.a), b = toTarget(s.b);
        const double len = std::hypot(b[0] - a[0], b[1] - a[1]);
        const int steps = std::max(1, static_cast<int>(std::ceil(len * 2.0)));
        for (int k = 0; k <= steps; ++k) {
            const double f = static_cast<double>(k) / steps;
            const double px = a[0] + f * (b[0] - a[0]);
            const double py = a[1] + f * (b[1] - a[1]);
            const double z = a[2] + f * (b[2] - a[2]);
            const int bx = static_cast<int>(std::floor(px - 0.5 * brush + 0.5));
            const int by = static_cast<int>(std::floor(py - 0.5 * brush + 0.5));
            for (int y = by; y < by + brush; ++y) {
                for (int x = bx; x < bx + brush; ++x) {
                    if (x < 0 || y < 0 || x >= t.w || y >= t.h) {
                        continue;
                    }
                    const std::size_t at =
                        static_cast<std::size_t>(y) * static_cast<std::size_t>(t.w) +
                        static_cast<std::size_t>(x);
                    if (z + bias >= t.depth[at]) {
                        t.color.set(x, y, s.color);
                    }
                }
            }
        }
    }
    return t.color.downsample(ss);
}

} // namespace uniforge::viz
