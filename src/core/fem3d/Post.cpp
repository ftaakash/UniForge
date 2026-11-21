#include "core/fem3d/Post.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace uniforge::fem3d {

namespace {

using std::int32_t;
using std::int64_t;
using std::size_t;

size_t sz(int32_t v) {
    return static_cast<size_t>(v);
}
size_t sz(int64_t v) {
    return static_cast<size_t>(v);
}

constexpr double kGaussOffset = 0.28867513459481288225; // 1/(2√3)
constexpr std::array<double, 2> kGauss{0.5 - kGaussOffset, 0.5 + kGaussOffset};

double linear(int corner, double t) {
    return corner == 0 ? 1.0 - t : t;
}

// Candidate cell indices along one axis for coordinate v: the cell containing v, and the one
// before it when v lies exactly on a line. Empty when v is outside the grid.
std::vector<int32_t> candidates(const std::vector<double>& lines, double v) {
    std::vector<int32_t> c;
    if (lines.size() < 2 || v < lines.front() || v > lines.back()) {
        return c;
    }
    const auto it = std::upper_bound(lines.begin(), lines.end(), v);
    int32_t i = static_cast<int32_t>(it - lines.begin()) - 1;
    const int32_t last = static_cast<int32_t>(lines.size()) - 2;
    i = std::min(i, last);
    c.push_back(i);
    if (i > 0 && v == lines[sz(i)]) {
        c.push_back(i - 1);
    }
    return c;
}

} // namespace

EnergyAudit energyAudit(const Design& design, const HexMesh& mesh, const LinearSystem& system,
                        const Vector& T, const AssemblyOptions& options) {
    EnergyAudit e;
    e.heatIn = system.heatIn;
    for (const BoundaryFace& face : mesh.boundaryFaces) {
        const FaceBc& bc = design.boxes[sz(face.box)].faces[static_cast<size_t>(face.side)];
        if (bc.kind != BcKind::Convection) {
            continue;
        }
        const auto fn = mesh.faceNodes(face);
        const double area = mesh.faceArea(face);
        const Mat4 He = faceConvection(bc.h, area);
        Vec4 ge{};
        if (options.ambient) {
            std::array<Point, 4> corners{};
            for (size_t a = 0; a < 4; ++a) {
                corners[a] = mesh.nodeXyz[sz(fn[a])];
            }
            ge = faceLoad(bc.h, options.ambient, corners);
        } else {
            ge = faceUniformLoad(bc.h * bc.tInf, area);
        }
        for (size_t a = 0; a < 4; ++a) {
            for (size_t b = 0; b < 4; ++b) {
                e.heatConvected += He[a][b] * T[sz(fn[b])];
            }
            e.heatConvected -= ge[a];
        }
    }
    const Vector r = reactions(system, T);
    for (const double v : r) {
        e.heatOutDirichlet -= v;
    }
    e.imbalance = e.heatIn - e.heatConvected - e.heatOutDirichlet;
    const double scale =
        std::max(std::fabs(e.heatIn), std::fabs(e.heatConvected) + std::fabs(e.heatOutDirichlet));
    e.relImbalance = scale > 0.0 ? std::fabs(e.imbalance) / scale : 0.0;
    return e;
}

Vector reactions(const LinearSystem& system, const Vector& T) {
    const CsrMatrix& R = system.dirichletRows;
    Vector r(sparse::toSize(R.rows), 0.0);
    for (sparse::Index i = 0; i < R.rows; ++i) {
        double s = 0.0;
        for (sparse::Offset k = R.rowPtr[sparse::toSize(i)]; k < R.rowPtr[sparse::toSize(i) + 1];
             ++k) {
            s += R.val[sparse::toSize(k)] * T[sparse::toSize(R.col[sparse::toSize(k)])];
        }
        r[sparse::toSize(i)] = s - system.dirichletLoad[sparse::toSize(i)];
    }
    return r;
}

Peak peakTemperature(const HexMesh& mesh, const Vector& T) {
    Peak p;
    for (size_t i = 0; i < T.size(); ++i) {
        if (p.node < 0 || T[i] > p.t) {
            p.t = T[i];
            p.node = static_cast<int32_t>(i);
            p.at = mesh.nodeXyz[i];
        }
    }
    return p;
}

Result<double> sampleTemperature(const HexMesh& mesh, const Vector& T, double x, double y,
                                 double z) {
    const std::array<double, 3> p{x, y, z};
    std::array<std::vector<int32_t>, 3> c;
    for (size_t a = 0; a < 3; ++a) {
        c[a] = candidates(mesh.lines[a], p[a]);
    }
    for (const int32_t k : c[2]) {
        for (const int32_t j : c[1]) {
            for (const int32_t i : c[0]) {
                const int64_t cell = mesh.cellIndex(i, j, k);
                if (mesh.cellBox[sz(cell)] < 0) {
                    continue;
                }
                const std::array<int32_t, 3> ijk{i, j, k};
                std::array<double, 3> t{};
                for (size_t a = 0; a < 3; ++a) {
                    const double lo = mesh.lines[a][sz(ijk[a])];
                    const double hi = mesh.lines[a][sz(ijk[a]) + 1];
                    t[a] = (p[a] - lo) / (hi - lo);
                }
                const auto nodes = mesh.cellNodes(cell);
                double v = 0.0;
                for (int n = 0; n < 8; ++n) {
                    const auto cn = hexCorner(n);
                    v += T[sz(nodes[static_cast<size_t>(n)])] * linear(cn[0], t[0]) *
                         linear(cn[1], t[1]) * linear(cn[2], t[2]);
                }
                return v;
            }
        }
    }
    return makeError(ErrorCode::InvalidInput,
                     "Point (" + std::to_string(x) + ", " + std::to_string(y) + ", " +
                         std::to_string(z) + ") m is outside the body.",
                     "Sample inside the meshed design.");
}

ErrorNorms errorNorms(const HexMesh& mesh, const Vector& T, const ScalarField& exact) {
    ErrorNorms e;
    double sumSq = 0.0;
    for (int64_t cell = 0; cell < static_cast<int64_t>(mesh.cellBox.size()); ++cell) {
        if (mesh.cellBox[sz(cell)] < 0) {
            continue;
        }
        const auto d = mesh.cellSize(cell);
        const auto nodes = mesh.cellNodes(cell);
        const auto& o = mesh.nodeXyz[sz(nodes[0])];
        const double w = d[0] * d[1] * d[2] / 8.0;
        e.volume += d[0] * d[1] * d[2];
        for (const double gz : kGauss) {
            for (const double gy : kGauss) {
                for (const double gx : kGauss) {
                    double th = 0.0;
                    for (int n = 0; n < 8; ++n) {
                        const auto cn = hexCorner(n);
                        th += T[sz(nodes[static_cast<size_t>(n)])] * linear(cn[0], gx) *
                              linear(cn[1], gy) * linear(cn[2], gz);
                    }
                    const double diff =
                        th - exact(o[0] + gx * d[0], o[1] + gy * d[1], o[2] + gz * d[2]);
                    sumSq += diff * diff * w;
                }
            }
        }
    }
    e.l2 = std::sqrt(sumSq);
    for (size_t i = 0; i < T.size(); ++i) {
        const auto& p = mesh.nodeXyz[i];
        e.maxNodal = std::max(e.maxNodal, std::fabs(T[i] - exact(p[0], p[1], p[2])));
    }
    return e;
}

} // namespace uniforge::fem3d
