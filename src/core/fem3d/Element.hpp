#pragma once

#include <array>
#include <functional>

namespace uniforge::fem3d {

// Element matrices of the trilinear hexahedron (Q1) on an axis-aligned box and of the bilinear
// quadrilateral on a rectangular boundary face (build spec §3.3).
//
// On a box the shape functions are products of 1D linear functions, so the matrices are
// tensor products of the 1D stiffness S(h) = (1/h)·[[1, −1], [−1, 1]] and mass
// M(h) = (h/6)·[[2, 1], [1, 2]]:
//
//     Ke = k · (Sx⊗My⊗Mz + Mx⊗Sy⊗Mz + Mx⊗My⊗Sz)     (exact; equal to 2×2×2 Gauss)
//
// Local node order is that of HexMesh::cellNodes(): bottom face (z = 0) (0,0), (1,0), (1,1),
// (0,1) in (x, y), then the same at z = 1. Face nodes are in the cyclic order of
// HexMesh::faceNodes().

using Mat8 = std::array<std::array<double, 8>, 8>;
using Vec8 = std::array<double, 8>;
using Mat4 = std::array<std::array<double, 4>, 4>;
using Vec4 = std::array<double, 4>;
using Point = std::array<double, 3>;
using ScalarField = std::function<double(double x, double y, double z)>;

// (ix, iy, iz) ∈ {0,1}³ of local node a.
[[nodiscard]] std::array<int, 3> hexCorner(int a) noexcept;

// Conduction stiffness ∫ k ∇Nᵢ·∇Nⱼ dV of a box with edges dx, dy, dz (W/K).
[[nodiscard]] Mat8 hexStiffness(double k, double dx, double dy, double dz) noexcept;

// Consistent load ∫ q Nᵢ dV for uniform q: q·V/8 per node (W).
[[nodiscard]] Vec8 hexUniformLoad(double q, double dx, double dy, double dz) noexcept;

// ∫ q(x) Nᵢ dV by 2×2×2 Gauss quadrature, box from `origin` with edges dx, dy, dz. Returns the
// load and adds ∫ q dV (same quadrature) to `total`.
[[nodiscard]] Vec8 hexLoad(const ScalarField& q, const Point& origin, double dx, double dy,
                           double dz, double& total);

// Convection matrix ∫ h NᵢNⱼ dA of a rectangle of area A (cyclic node order):
// (h·A/36)·[[4,2,1,2],[2,4,2,1],[1,2,4,2],[2,1,2,4]] (W/K).
[[nodiscard]] Mat4 faceConvection(double h, double area) noexcept;

// ∫ h·T∞ Nᵢ dA for uniform h·T∞: h·T∞·A/4 per node (W).
[[nodiscard]] Vec4 faceUniformLoad(double hTinf, double area) noexcept;

// ∫ h·T∞(x) Nᵢ dA by 2×2 Gauss on the rectangle with cyclic corners p0 … p3.
[[nodiscard]] Vec4 faceLoad(double h, const ScalarField& tInf, const std::array<Point, 4>& corners);

} // namespace uniforge::fem3d
