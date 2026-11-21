#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/common/Result.hpp"
#include "core/fem3d/Design.hpp"
#include "core/fem3d/Element.hpp"
#include "core/fem3d/HexMesh.hpp"
#include "core/sparse/CsrMatrix.hpp"
#include "core/sparse/VectorOps.hpp"

namespace uniforge::fem3d {

using sparse::CsrMatrix;
using sparse::Vector;

// Optional overrides of the design's data, used by the manufactured-solution verification
// (build spec §3.6). Empty = take the value from the design.
struct AssemblyOptions {
    ScalarField source;    // q(x, y, z) instead of each material's uniform q
    ScalarField ambient;   // T∞(x, y, z) on convective faces instead of the face's tInf
    ScalarField dirichlet; // T(x, y, z) on Dirichlet faces instead of the face's t
    // false keeps the assembled K and f unmodified (for tests of the raw system); the Dirichlet
    // nodes and values are still recorded.
    bool applyDirichlet = true;
};

// K·T = f of the 3D steady conduction problem (build spec §3.3, §3.5, §6.1).
struct LinearSystem {
    CsrMatrix K; // conduction + convection; Dirichlet rows/columns eliminated symmetrically
    Vector f;
    std::vector<std::uint8_t> freeMask; // 1 = free node, 0 = prescribed temperature
    std::vector<std::int32_t> dirichletNodes;
    Vector dirichletValues;
    // Unconstrained rows of the Dirichlet nodes, saved before elimination, for the reactions
    // r_d = K_d·T − f_d (v1 DirichletBC logic): row i belongs to dirichletNodes[i].
    CsrMatrix dirichletRows;
    Vector dirichletLoad;
    double heatIn = 0.0; // ∫ q dV, W (the same quadrature as f)

    // Assembly cost, reported apart from the solvers (amendment A4).
    double assemblySeconds = 0.0;
    std::size_t matrixBytes = 0;
};

// Assembles straight into CSR (amendment A4): the pattern comes from the grid connectivity
// (each node couples with the nodes of its ≤ 8 cells), K is allocated once and every element
// matrix is added in place. Convective faces add h·∫NᵢNⱼ to K and h·T∞·∫Nᵢ to f. Dirichlet
// nodes are eliminated symmetrically: their columns move to f, their rows become identity rows
// with f_d = T_d, so K stays symmetric positive definite.
// Errors: InvalidInput when a node gets two different Dirichlet values or the mesh does not
// belong to the design; LimitExceeded if the matrix would need more than 2^31 − 1 columns.
[[nodiscard]] Result<LinearSystem> assemble(const Design& design, const HexMesh& mesh,
                                            const AssemblyOptions& options = {});

} // namespace uniforge::fem3d
