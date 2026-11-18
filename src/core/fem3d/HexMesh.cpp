#include "core/fem3d/HexMesh.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace uniforge::fem3d {

namespace {

using std::int32_t;
using std::int64_t;
using std::size_t;

constexpr double kMergeTolerance = 1e-12; // m: mandatory lines closer than this are one line
constexpr int kMaxGradingSweeps = 200;

size_t sz(int64_t v) {
    return static_cast<size_t>(v);
}
size_t sz(int32_t v) {
    return static_cast<size_t>(v);
}

Error invalidSpec(const std::string& message) {
    return makeError(ErrorCode::InvalidInput, message,
                     "Use a positive target spacing, a grade ratio above 1 and 1–10 layer cells.");
}

Result<void> checkSpec(const MeshSpec& spec) {
    if (!std::isfinite(spec.targetSpacing) || spec.targetSpacing <= 0.0) {
        return invalidSpec("Mesh target spacing " + std::to_string(spec.targetSpacing) +
                           " m is not a positive number.");
    }
    if (!std::isfinite(spec.maxGradeRatio) || spec.maxGradeRatio <= 1.0) {
        return invalidSpec("Mesh grade ratio " + std::to_string(spec.maxGradeRatio) +
                           " must be larger than 1.");
    }
    if (spec.fixedLayerCells < 1 || spec.fixedLayerCells > 10) {
        return invalidSpec("Fixed-layer cell count " + std::to_string(spec.fixedLayerCells) +
                           " is outside 1–10.");
    }
    return {};
}

// Σ_{i<n} min(s, eL·ρ^i, eR·ρ^(n−1−i)): the cell sizes of a gap of n cells that start at eL,
// end at eR, grow by ρ per cell and never exceed s.
double gradedSum(int n, double s, double eL, double eR, double rho, std::vector<double>* out) {
    double sum = 0.0;
    if (out != nullptr) {
        out->clear();
    }
    for (int i = 0; i < n; ++i) {
        const double c = std::min({s, eL * std::pow(rho, i), eR * std::pow(rho, n - 1 - i)});
        sum += c;
        if (out != nullptr) {
            out->push_back(c);
        }
    }
    return sum;
}

// Cell sizes for one gap of length L with end-size limits eL, eR (≤ s), maximum size s and
// growth ratio R. Picks the fewest cells for which geometric growth up to R can fill the gap,
// then the growth ρ ∈ [1, R] that fills it exactly (bisection), so the end cells are as large
// as their limits allow. If even equal cells at the smallest limit overfill the gap, the cells
// are equal (ρ = 1) and shrink uniformly.
std::vector<double> fillGap(double L, double s, double eL, double eR, double R) {
    eL = std::min(eL, s);
    eR = std::min(eR, s);
    // Smallest n with gradedSum(n) ≥ L. The sum grows with n (every term is non-decreasing in n
    // and a term is added), so double n until it fits, then bisect.
    int hiN = 1;
    while (gradedSum(hiN, s, eL, eR, R, nullptr) < L) {
        hiN *= 2;
    }
    int loN = hiN / 2; // gradedSum(loN) < L, or loN == 0
    while (hiN - loN > 1) {
        const int mid = loN + (hiN - loN) / 2;
        if (gradedSum(mid, s, eL, eR, R, nullptr) < L) {
            loN = mid;
        } else {
            hiN = mid;
        }
    }
    const int n = hiN;
    std::vector<double> sizes;
    const double m = std::min(eL, eR);
    if (n * m >= L) {
        sizes.assign(sz(static_cast<int32_t>(n)), L / n);
        return sizes;
    }
    double lo = 1.0;
    double hi = R;
    for (int it = 0; it < 80; ++it) {
        const double mid = 0.5 * (lo + hi);
        if (gradedSum(n, s, eL, eR, mid, nullptr) < L) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    const double sum = gradedSum(n, s, eL, eR, hi, &sizes);
    const double scale = L / sum; // ≤ 1, within bisection precision of 1
    for (double& c : sizes) {
        c *= scale;
    }
    return sizes;
}

bool sameSizes(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::fabs(a[i] - b[i]) > 1e-12 * std::max(std::fabs(a[i]), std::fabs(b[i]))) {
            return false;
        }
    }
    return true;
}

// Index of the line equal to x (within the merge tolerance), or −1.
int32_t lineIndex(const std::vector<double>& lines, double x) {
    const auto it = std::lower_bound(lines.begin(), lines.end(), x - 1e-9);
    if (it == lines.end() || std::fabs(*it - x) > 1e-9) {
        return -1;
    }
    return static_cast<int32_t>(it - lines.begin());
}

} // namespace

// ---- HexMesh helpers ------------------------------------------------------------------------

int32_t HexMesh::cellsAlong(int axis) const noexcept {
    return static_cast<int32_t>(lines[sz(static_cast<int32_t>(axis))].size()) - 1;
}

int64_t HexMesh::cellIndex(int32_t i, int32_t j, int32_t k) const noexcept {
    const int64_t nx = cellsAlong(0);
    const int64_t ny = cellsAlong(1);
    return i + nx * (j + ny * static_cast<int64_t>(k));
}

int64_t HexMesh::pointIndex(int32_t i, int32_t j, int32_t k) const noexcept {
    const int64_t px = cellsAlong(0) + 1;
    const int64_t py = cellsAlong(1) + 1;
    return i + px * (j + py * static_cast<int64_t>(k));
}

std::array<int32_t, 3> HexMesh::cellIjk(int64_t cell) const noexcept {
    const int64_t nx = cellsAlong(0);
    const int64_t ny = cellsAlong(1);
    return {static_cast<int32_t>(cell % nx), static_cast<int32_t>((cell / nx) % ny),
            static_cast<int32_t>(cell / (nx * ny))};
}

std::array<int32_t, 8> HexMesh::cellNodes(int64_t cell) const noexcept {
    const auto [i, j, k] = cellIjk(cell);
    const auto node = [&](int32_t di, int32_t dj, int32_t dk) {
        return nodeOfGridPoint[sz(pointIndex(i + di, j + dj, k + dk))];
    };
    return {node(0, 0, 0), node(1, 0, 0), node(1, 1, 0), node(0, 1, 0),
            node(0, 0, 1), node(1, 0, 1), node(1, 1, 1), node(0, 1, 1)};
}

std::array<double, 3> HexMesh::cellSize(int64_t cell) const noexcept {
    const auto ijk = cellIjk(cell);
    std::array<double, 3> d{};
    for (size_t a = 0; a < 3; ++a) {
        d[a] = lines[a][sz(ijk[a]) + 1] - lines[a][sz(ijk[a])];
    }
    return d;
}

double HexMesh::cellVolume(int64_t cell) const noexcept {
    const auto d = cellSize(cell);
    return d[0] * d[1] * d[2];
}

double HexMesh::faceArea(const BoundaryFace& face) const noexcept {
    const auto d = cellSize(face.cell);
    const size_t normalAxis = sz(static_cast<int32_t>(face.side / 2));
    double a = 1.0;
    for (size_t ax = 0; ax < 3; ++ax) {
        if (ax != normalAxis) {
            a *= d[ax];
        }
    }
    return a;
}

std::array<int32_t, 4> HexMesh::faceNodes(const BoundaryFace& face) const noexcept {
    // Corner numbers of cellNodes(), ordered counter-clockwise seen from outside.
    static constexpr std::array<std::array<int, 4>, kSides> kCorners{{
        {0, 4, 7, 3}, // XMin
        {1, 2, 6, 5}, // XMax
        {0, 1, 5, 4}, // YMin
        {3, 7, 6, 2}, // YMax
        {0, 3, 2, 1}, // ZMin
        {4, 5, 6, 7}, // ZMax
    }};
    const auto nodes = cellNodes(face.cell);
    const auto& c = kCorners[sz(static_cast<int32_t>(face.side))];
    return {nodes[sz(static_cast<int32_t>(c[0]))], nodes[sz(static_cast<int32_t>(c[1]))],
            nodes[sz(static_cast<int32_t>(c[2]))], nodes[sz(static_cast<int32_t>(c[3]))]};
}

// ---- Axis grading ---------------------------------------------------------------------------

Result<std::vector<double>> buildAxis(std::vector<double> mandatory,
                                      const std::vector<std::pair<double, double>>& fixed,
                                      const MeshSpec& spec) {
    if (auto r = checkSpec(spec); !r) {
        return std::move(r).error();
    }
    std::sort(mandatory.begin(), mandatory.end());
    std::vector<double> m;
    for (const double x : mandatory) {
        if (!std::isfinite(x)) {
            return invalidSpec("A grid line coordinate is not finite.");
        }
        if (m.empty() || x - m.back() > kMergeTolerance) {
            m.push_back(x);
        }
    }
    if (m.size() < 2) {
        return invalidSpec("An axis needs at least two distinct grid lines.");
    }

    const size_t gaps = m.size() - 1;
    const double s = spec.targetSpacing;
    const double R = spec.maxGradeRatio;
    std::vector<bool> isFixed(gaps, false);
    std::vector<std::vector<double>> sizes(gaps);
    for (size_t g = 0; g < gaps; ++g) {
        const double a = m[g];
        const double b = m[g + 1];
        for (const auto& [lo, hi] : fixed) {
            if (a >= lo - 1e-9 && b <= hi + 1e-9) {
                isFixed[g] = true;
            }
        }
        if (isFixed[g]) {
            sizes[g].assign(sz(static_cast<int32_t>(spec.fixedLayerCells)),
                            (b - a) / spec.fixedLayerCells);
        } else {
            sizes[g] = fillGap(b - a, s, s, s, R);
        }
    }
    // Grade: each gap's end cells may be at most R times their neighbours'. Limits only shrink,
    // so the sweeps converge; the cap is a safety net (the result is checked by the caller's
    // statistics and by the tests).
    for (int sweep = 0; sweep < kMaxGradingSweeps; ++sweep) {
        bool changed = false;
        for (size_t g = 0; g < gaps; ++g) {
            if (isFixed[g]) {
                continue;
            }
            const double eL = g > 0 ? R * sizes[g - 1].back() : s;
            const double eR = g + 1 < gaps ? R * sizes[g + 1].front() : s;
            std::vector<double> next = fillGap(m[g + 1] - m[g], s, eL, eR, R);
            if (!sameSizes(next, sizes[g])) {
                sizes[g] = std::move(next);
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }

    std::vector<double> lines{m.front()};
    for (size_t g = 0; g < gaps; ++g) {
        double x = m[g];
        for (size_t c = 0; c + 1 < sizes[g].size(); ++c) {
            x += sizes[g][c];
            lines.push_back(x);
        }
        lines.push_back(m[g + 1]); // exact mandatory line, no accumulated rounding
    }
    return lines;
}

// ---- Mesh -----------------------------------------------------------------------------------

Result<HexMesh> buildMesh(const Design& design, const MeshSpec& spec) {
    if (auto r = validate(design); !r) {
        return std::move(r).error();
    }
    if (auto r = checkSpec(spec); !r) {
        return std::move(r).error();
    }

    // Refuse absurd spacings before any line is generated: the grid has at least extent/s cells
    // along each axis.
    {
        double estimate = 1.0;
        for (size_t a = 0; a < 3; ++a) {
            double lo = std::numeric_limits<double>::infinity();
            double hi = -std::numeric_limits<double>::infinity();
            for (const Box& b : design.boxes) {
                lo = std::min(lo, b.min[a]);
                hi = std::max(hi, b.max[a]);
            }
            estimate *= (hi - lo) / spec.targetSpacing + 1.0;
        }
        if (estimate > static_cast<double>(MeshSpec::kMaxGridPoints)) {
            return makeError(ErrorCode::LimitExceeded,
                             "The mesh grid would have more than " +
                                 std::to_string(MeshSpec::kMaxGridPoints) + " points.",
                             "Use a larger target spacing.");
        }
    }

    HexMesh mesh;
    for (int axis = 0; axis < 3; ++axis) {
        const size_t a = sz(static_cast<int32_t>(axis));
        std::vector<double> mandatory;
        for (const Box& b : design.boxes) {
            mandatory.push_back(b.min[a]);
            mandatory.push_back(b.max[a]);
        }
        std::vector<std::pair<double, double>> fixed;
        for (const FixedLayer& l : design.fixedLayers) {
            if (l.axis == axis) {
                fixed.emplace_back(l.lo, l.hi);
            }
        }
        auto lines = buildAxis(std::move(mandatory), fixed, spec);
        if (!lines) {
            return std::move(lines).error();
        }
        mesh.lines[a] = std::move(lines).value();
    }

    const int64_t nx = mesh.cellsAlong(0);
    const int64_t ny = mesh.cellsAlong(1);
    const int64_t nz = mesh.cellsAlong(2);
    const int64_t points = (nx + 1) * (ny + 1) * (nz + 1);
    if (points > MeshSpec::kMaxGridPoints) {
        return makeError(ErrorCode::LimitExceeded,
                         "The mesh grid would have " + std::to_string(points) +
                             " points, more than the limit of " +
                             std::to_string(MeshSpec::kMaxGridPoints) + ".",
                         "Use a larger target spacing.");
    }
    mesh.stats.gridCells = {static_cast<int32_t>(nx), static_cast<int32_t>(ny),
                            static_cast<int32_t>(nz)};

    // Cell ownership: every cell between a box's lines belongs to that box.
    mesh.cellBox.assign(sz(nx * ny * nz), -1);
    mesh.boxMaterial.reserve(design.boxes.size());
    for (size_t b = 0; b < design.boxes.size(); ++b) {
        const Box& box = design.boxes[b];
        mesh.boxMaterial.push_back(box.material);
        std::array<int32_t, 3> lo{};
        std::array<int32_t, 3> hi{};
        for (size_t a = 0; a < 3; ++a) {
            lo[a] = lineIndex(mesh.lines[a], box.min[a]);
            hi[a] = lineIndex(mesh.lines[a], box.max[a]);
            if (lo[a] < 0 || hi[a] < 0) {
                return makeError(ErrorCode::Internal,
                                 "Box '" + box.name + "' does not lie on the grid lines.",
                                 "Report this as a bug.");
            }
        }
        for (int32_t k = lo[2]; k < hi[2]; ++k) {
            for (int32_t j = lo[1]; j < hi[1]; ++j) {
                for (int32_t i = lo[0]; i < hi[0]; ++i) {
                    mesh.cellBox[sz(mesh.cellIndex(i, j, k))] = static_cast<int32_t>(b);
                }
            }
        }
    }

    // Nodes: grid points touching at least one active cell, numbered with i fastest.
    mesh.nodeOfGridPoint.assign(sz(points), -1);
    const auto active = [&](int64_t i, int64_t j, int64_t k) {
        if (i < 0 || j < 0 || k < 0 || i >= nx || j >= ny || k >= nz) {
            return false;
        }
        return mesh.cellBox[sz(i + nx * (j + ny * k))] >= 0;
    };
    int64_t nodeCount = 0;
    for (int64_t k = 0; k <= nz; ++k) {
        for (int64_t j = 0; j <= ny; ++j) {
            for (int64_t i = 0; i <= nx; ++i) {
                bool used = false;
                for (int64_t dk = -1; dk <= 0 && !used; ++dk) {
                    for (int64_t dj = -1; dj <= 0 && !used; ++dj) {
                        for (int64_t di = -1; di <= 0 && !used; ++di) {
                            used = active(i + di, j + dj, k + dk);
                        }
                    }
                }
                if (!used) {
                    continue;
                }
                if (nodeCount >= std::numeric_limits<int32_t>::max()) {
                    return makeError(ErrorCode::LimitExceeded,
                                     "The mesh would have more than 2^31 nodes.",
                                     "Use a larger target spacing.");
                }
                mesh.nodeOfGridPoint[sz(i + (nx + 1) * (j + (ny + 1) * k))] =
                    static_cast<int32_t>(nodeCount++);
                mesh.nodeXyz.push_back(
                    {mesh.lines[0][sz(i)], mesh.lines[1][sz(j)], mesh.lines[2][sz(k)]});
                mesh.nodeGrid.push_back(
                    {static_cast<int32_t>(i), static_cast<int32_t>(j), static_cast<int32_t>(k)});
            }
        }
    }

    // Boundary faces and cell statistics.
    double aspectSum = 0.0;
    mesh.stats.minAspect = std::numeric_limits<double>::infinity();
    static constexpr std::array<std::array<int64_t, 3>, kSides> kStep{
        {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}}};
    for (int64_t k = 0; k < nz; ++k) {
        for (int64_t j = 0; j < ny; ++j) {
            for (int64_t i = 0; i < nx; ++i) {
                const int64_t cell = i + nx * (j + ny * k);
                const int32_t box = mesh.cellBox[sz(cell)];
                if (box < 0) {
                    continue;
                }
                ++mesh.stats.activeCells;
                const auto d = mesh.cellSize(cell);
                const double aspect = std::max({d[0], d[1], d[2]}) / std::min({d[0], d[1], d[2]});
                mesh.stats.minAspect = std::min(mesh.stats.minAspect, aspect);
                mesh.stats.maxAspect = std::max(mesh.stats.maxAspect, aspect);
                aspectSum += aspect;
                for (int side = 0; side < kSides; ++side) {
                    const auto& st = kStep[sz(static_cast<int32_t>(side))];
                    if (!active(i + st[0], j + st[1], k + st[2])) {
                        mesh.boundaryFaces.push_back({cell, static_cast<Side>(side), box});
                    }
                }
            }
        }
    }
    mesh.stats.nodes = nodeCount;
    mesh.stats.meanAspect =
        mesh.stats.activeCells > 0 ? aspectSum / static_cast<double>(mesh.stats.activeCells) : 0.0;
    if (mesh.stats.activeCells == 0) {
        mesh.stats.minAspect = 0.0;
    }
    for (const auto& l : mesh.lines) {
        for (size_t c = 1; c + 1 < l.size(); ++c) {
            const double left = l[c] - l[c - 1];
            const double right = l[c + 1] - l[c];
            mesh.stats.maxGrade =
                std::max(mesh.stats.maxGrade, std::max(left / right, right / left));
        }
    }
    if (mesh.stats.maxGrade == 0.0) {
        mesh.stats.maxGrade = 1.0;
    }
    return mesh;
}

SurfaceMesh extractSurface(const HexMesh& mesh) {
    SurfaceMesh s;
    std::vector<int32_t> vertexOfNode(mesh.nodeXyz.size(), -1);
    const auto vertex = [&](int32_t node) {
        int32_t& v = vertexOfNode[sz(node)];
        if (v < 0) {
            v = static_cast<int32_t>(s.vertices.size());
            s.vertices.push_back(mesh.nodeXyz[sz(node)]);
            s.vertexNode.push_back(node);
        }
        return v;
    };
    s.triangles.reserve(2 * mesh.boundaryFaces.size());
    for (size_t f = 0; f < mesh.boundaryFaces.size(); ++f) {
        const auto n = mesh.faceNodes(mesh.boundaryFaces[f]);
        const std::array<int32_t, 4> v{vertex(n[0]), vertex(n[1]), vertex(n[2]), vertex(n[3])};
        s.triangles.push_back({v[0], v[1], v[2]});
        s.triangles.push_back({v[0], v[2], v[3]});
        s.triangleFace.push_back(static_cast<int32_t>(f));
        s.triangleFace.push_back(static_cast<int32_t>(f));
    }
    return s;
}

} // namespace uniforge::fem3d
