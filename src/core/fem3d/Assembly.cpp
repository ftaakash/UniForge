#include "core/fem3d/Assembly.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <string>

namespace uniforge::fem3d {

namespace {

using sparse::Index;
using sparse::Offset;
using sparse::toSize;
using std::int32_t;
using std::int64_t;
using std::size_t;

size_t sz(int32_t v) {
    return static_cast<size_t>(v);
}
size_t sz(int64_t v) {
    return static_cast<size_t>(v);
}

Error invalid(const std::string& message) {
    return makeError(ErrorCode::InvalidInput, message,
                     "Check the design and mesh passed to assemble(); nothing was solved.");
}

// Adds `value` to K(row, col). The entry exists by construction of the pattern.
void addEntry(CsrMatrix& K, int32_t row, int32_t col, double value) {
    const Offset k = K.find(row, col);
    K.val[toSize(k)] += value;
}

} // namespace

Result<LinearSystem> assemble(const Design& design, const HexMesh& mesh,
                              const AssemblyOptions& options) {
    const auto t0 = std::chrono::steady_clock::now();
    if (mesh.boxMaterial.size() != design.boxes.size()) {
        return invalid("The mesh was built from a design with " +
                       std::to_string(mesh.boxMaterial.size()) + " boxes, not " +
                       std::to_string(design.boxes.size()) + ".");
    }
    if (mesh.nodeXyz.size() > static_cast<size_t>(std::numeric_limits<Index>::max())) {
        return makeError(ErrorCode::LimitExceeded,
                         "The mesh has too many nodes for 32-bit columns.",
                         "Use a larger target spacing.");
    }
    const auto n = static_cast<Index>(mesh.nodeXyz.size());
    const int64_t nx = mesh.cellsAlong(0);
    const int64_t ny = mesh.cellsAlong(1);
    const int64_t nz = mesh.cellsAlong(2);

    // ---- Pattern from grid connectivity -------------------------------------------------------
    auto pattern = sparse::buildPattern(n, n, [&](Index node, std::vector<Index>& out) {
        const auto& g = mesh.nodeGrid[toSize(node)];
        for (int64_t dk = -1; dk <= 0; ++dk) {
            for (int64_t dj = -1; dj <= 0; ++dj) {
                for (int64_t di = -1; di <= 0; ++di) {
                    const int64_t i = g[0] + di;
                    const int64_t j = g[1] + dj;
                    const int64_t k = g[2] + dk;
                    if (i < 0 || j < 0 || k < 0 || i >= nx || j >= ny || k >= nz) {
                        continue;
                    }
                    const int64_t cell = i + nx * (j + ny * k);
                    if (mesh.cellBox[sz(cell)] < 0) {
                        continue;
                    }
                    for (const int32_t m : mesh.cellNodes(cell)) {
                        out.push_back(m);
                    }
                }
            }
        }
    });
    if (!pattern) {
        return std::move(pattern).error();
    }

    LinearSystem sys;
    sys.K = sparse::makeMatrix(std::move(pattern).value());
    sys.f.assign(toSize(n), 0.0);
    CsrMatrix& K = sys.K;

    // ---- Cells: conduction and heat generation ------------------------------------------------
    for (int64_t cell = 0; cell < static_cast<int64_t>(mesh.cellBox.size()); ++cell) {
        const int32_t box = mesh.cellBox[sz(cell)];
        if (box < 0) {
            continue;
        }
        const Material& mat = design.materials[sz(mesh.boxMaterial[sz(box)])];
        const auto d = mesh.cellSize(cell);
        const auto nodes = mesh.cellNodes(cell);
        const Mat8 Ke = hexStiffness(mat.k, d[0], d[1], d[2]);
        for (size_t a = 0; a < 8; ++a) {
            for (size_t b = 0; b < 8; ++b) {
                addEntry(K, nodes[a], nodes[b], Ke[a][b]);
            }
        }
        Vec8 fe{};
        if (options.source) {
            const auto& origin = mesh.nodeXyz[sz(nodes[0])];
            fe = hexLoad(options.source, origin, d[0], d[1], d[2], sys.heatIn);
        } else if (mat.q != 0.0) {
            fe = hexUniformLoad(mat.q, d[0], d[1], d[2]);
            sys.heatIn += mat.q * d[0] * d[1] * d[2];
        }
        for (size_t a = 0; a < 8; ++a) {
            sys.f[sz(nodes[a])] += fe[a];
        }
    }

    // ---- Boundary faces: convection and Dirichlet values ----------------------------------------
    std::vector<double> prescribed(toSize(n), std::numeric_limits<double>::quiet_NaN());
    for (const BoundaryFace& face : mesh.boundaryFaces) {
        const FaceBc& bc = design.boxes[sz(face.box)].faces[static_cast<size_t>(face.side)];
        const auto fn = mesh.faceNodes(face);
        if (bc.kind == BcKind::Convection) {
            const double area = mesh.faceArea(face);
            const Mat4 He = faceConvection(bc.h, area);
            for (size_t a = 0; a < 4; ++a) {
                for (size_t b = 0; b < 4; ++b) {
                    addEntry(K, fn[a], fn[b], He[a][b]);
                }
            }
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
                sys.f[sz(fn[a])] += ge[a];
            }
        } else if (bc.kind == BcKind::Dirichlet) {
            for (const int32_t node : fn) {
                const auto& p = mesh.nodeXyz[sz(node)];
                const double t = options.dirichlet ? options.dirichlet(p[0], p[1], p[2]) : bc.t;
                double& slot = prescribed[sz(node)];
                if (std::isnan(slot)) {
                    slot = t;
                } else if (std::fabs(slot - t) > 1e-12 * std::max(std::fabs(slot), std::fabs(t))) {
                    return invalid("Node " + std::to_string(node) +
                                   " lies on Dirichlet faces with different temperatures (" +
                                   std::to_string(slot) + " and " + std::to_string(t) + " °C).");
                }
            }
        }
    }

    // ---- Dirichlet: record, save unconstrained rows, eliminate symmetrically -------------------
    sys.freeMask.assign(toSize(n), 1);
    for (Index node = 0; node < n; ++node) {
        if (!std::isnan(prescribed[toSize(node)])) {
            sys.freeMask[toSize(node)] = 0;
            sys.dirichletNodes.push_back(node);
            sys.dirichletValues.push_back(prescribed[toSize(node)]);
        }
    }
    {
        std::vector<sparse::Triplet> rows;
        for (size_t i = 0; i < sys.dirichletNodes.size(); ++i) {
            const Index d = sys.dirichletNodes[i];
            for (Offset k = K.rowPtr[toSize(d)]; k < K.rowPtr[toSize(d) + 1]; ++k) {
                rows.push_back({static_cast<Index>(i), K.col[toSize(k)], K.val[toSize(k)]});
            }
            sys.dirichletLoad.push_back(sys.f[toSize(d)]);
        }
        auto saved =
            sparse::fromTriplets(static_cast<Index>(sys.dirichletNodes.size()), n, std::move(rows));
        if (!saved) {
            return std::move(saved).error();
        }
        sys.dirichletRows = std::move(saved).value();
    }
    if (options.applyDirichlet) {
        // Pass 1: move each prescribed column into f of the free rows and zero it there.
        for (size_t i = 0; i < sys.dirichletNodes.size(); ++i) {
            const Index d = sys.dirichletNodes[i];
            const double t = sys.dirichletValues[i];
            for (Offset k = K.rowPtr[toSize(d)]; k < K.rowPtr[toSize(d) + 1]; ++k) {
                const Index j = K.col[toSize(k)];
                if (sys.freeMask[toSize(j)] == 0) {
                    continue;
                }
                const Offset kjd = K.find(j, d); // symmetric pattern: (j, d) exists
                sys.f[toSize(j)] -= K.val[toSize(kjd)] * t;
                K.val[toSize(kjd)] = 0.0;
            }
        }
        // Pass 2: prescribed rows become identity rows.
        for (size_t i = 0; i < sys.dirichletNodes.size(); ++i) {
            const Index d = sys.dirichletNodes[i];
            for (Offset k = K.rowPtr[toSize(d)]; k < K.rowPtr[toSize(d) + 1]; ++k) {
                K.val[toSize(k)] = K.col[toSize(k)] == d ? 1.0 : 0.0;
            }
            sys.f[toSize(d)] = sys.dirichletValues[i];
        }
    }

    sys.matrixBytes = K.bytes();
    sys.assemblySeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return sys;
}

} // namespace uniforge::fem3d
