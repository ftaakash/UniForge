#include "core/fem3d/Element.hpp"

#include <cmath>
#include <cstddef>

namespace uniforge::fem3d {

namespace {

using std::size_t;

size_t at(int i) {
    return static_cast<size_t>(i);
}

// 1D linear element on [0, h], local nodes 0 and 1.
double stiffness1d(int i, int j, double h) {
    return (i == j ? 1.0 : -1.0) / h;
}
double mass1d(int i, int j, double h) {
    return h * (i == j ? 2.0 : 1.0) / 6.0;
}

// Two-point Gauss rule on [0, 1]: points 1/2 ∓ 1/(2√3), weights 1/2.
constexpr double kGaussOffset = 0.28867513459481288225; // 1/(2√3)
const std::array<double, 2> kGauss{0.5 - kGaussOffset, 0.5 + kGaussOffset};

double linear(int corner, double t) {
    return corner == 0 ? 1.0 - t : t;
}

} // namespace

std::array<int, 3> hexCorner(int a) noexcept {
    // 0:(0,0,0) 1:(1,0,0) 2:(1,1,0) 3:(0,1,0) 4:(0,0,1) 5:(1,0,1) 6:(1,1,1) 7:(0,1,1)
    const int low = a & 3;
    const int ix = (low == 1 || low == 2) ? 1 : 0;
    const int iy = (low == 2 || low == 3) ? 1 : 0;
    const int iz = a >= 4 ? 1 : 0;
    return {ix, iy, iz};
}

Mat8 hexStiffness(double k, double dx, double dy, double dz) noexcept {
    Mat8 K{};
    for (int a = 0; a < 8; ++a) {
        const auto ca = hexCorner(a);
        for (int b = 0; b < 8; ++b) {
            const auto cb = hexCorner(b);
            const double sx = stiffness1d(ca[0], cb[0], dx);
            const double sy = stiffness1d(ca[1], cb[1], dy);
            const double sz = stiffness1d(ca[2], cb[2], dz);
            const double mx = mass1d(ca[0], cb[0], dx);
            const double my = mass1d(ca[1], cb[1], dy);
            const double mz = mass1d(ca[2], cb[2], dz);
            K[at(a)][at(b)] = k * (sx * my * mz + mx * sy * mz + mx * my * sz);
        }
    }
    return K;
}

Vec8 hexUniformLoad(double q, double dx, double dy, double dz) noexcept {
    Vec8 f{};
    f.fill(q * dx * dy * dz / 8.0);
    return f;
}

Vec8 hexLoad(const ScalarField& q, const Point& origin, double dx, double dy, double dz,
             double& total) {
    Vec8 f{};
    const double w = dx * dy * dz / 8.0; // weight of each of the 8 Gauss points
    for (const double gz : kGauss) {
        for (const double gy : kGauss) {
            for (const double gx : kGauss) {
                const double value =
                    q(origin[0] + gx * dx, origin[1] + gy * dy, origin[2] + gz * dz) * w;
                total += value;
                for (int a = 0; a < 8; ++a) {
                    const auto c = hexCorner(a);
                    f[at(a)] += value * linear(c[0], gx) * linear(c[1], gy) * linear(c[2], gz);
                }
            }
        }
    }
    return f;
}

Mat4 faceConvection(double h, double area) noexcept {
    static constexpr std::array<std::array<double, 4>, 4> kPattern{
        {{4, 2, 1, 2}, {2, 4, 2, 1}, {1, 2, 4, 2}, {2, 1, 2, 4}}};
    Mat4 H{};
    for (size_t i = 0; i < 4; ++i) {
        for (size_t j = 0; j < 4; ++j) {
            H[i][j] = h * area * kPattern[i][j] / 36.0;
        }
    }
    return H;
}

Vec4 faceUniformLoad(double hTinf, double area) noexcept {
    Vec4 g{};
    g.fill(hTinf * area / 4.0);
    return g;
}

Vec4 faceLoad(double h, const ScalarField& tInf, const std::array<Point, 4>& c) {
    // Bilinear map of the unit square: p(s, t) = (1−s)(1−t)·p0 + s(1−t)·p1 + s·t·p2 + (1−s)·t·p3,
    // with shape functions in the same order. The face is a rectangle, so dA = A ds dt.
    const auto dist = [](const Point& a, const Point& b) {
        return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) +
                         (a[2] - b[2]) * (a[2] - b[2]));
    };
    const double area = dist(c[0], c[1]) * dist(c[1], c[2]);
    Vec4 g{};
    for (const double t : kGauss) {
        for (const double s : kGauss) {
            const std::array<double, 4> N{(1 - s) * (1 - t), s * (1 - t), s * t, (1 - s) * t};
            Point p{};
            for (size_t a = 0; a < 4; ++a) {
                for (size_t d = 0; d < 3; ++d) {
                    p[d] += N[a] * c[a][d];
                }
            }
            const double value = h * tInf(p[0], p[1], p[2]) * area / 4.0;
            for (size_t a = 0; a < 4; ++a) {
                g[a] += value * N[a];
            }
        }
    }
    return g;
}

} // namespace uniforge::fem3d
