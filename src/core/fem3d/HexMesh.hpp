#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "core/common/Result.hpp"
#include "core/fem3d/Design.hpp"

namespace uniforge::fem3d {

// Mesh controls (build spec §3.4). The only size parameter is the target spacing s.
struct MeshSpec {
    double targetSpacing = 1e-3; // m, largest cell edge
    double maxGradeRatio = 1.5;  // largest size ratio of neighbouring cells along an axis
    int fixedLayerCells = 1;     // cells across each Design::fixedLayers entry (1 or 2 in V2)

    // Refusal limit, so a typo in s cannot allocate tens of GB (about 4 GB of grid arrays).
    static constexpr std::int64_t kMaxGridPoints = 400'000'000;
};

// One exposed face of an active cell (build spec §3.4, §6.1).
struct BoundaryFace {
    std::int64_t cell = 0; // linear grid cell index
    Side side = XMin;
    std::int32_t box = 0; // the box whose face it lies on (gives the boundary condition)
};

struct MeshStats {
    std::array<std::int32_t, 3> gridCells{}; // tensor-grid cells per axis (active or not)
    std::int64_t activeCells = 0;
    std::int64_t nodes = 0;
    double minAspect = 0.0; // per cell: longest edge / shortest edge
    double maxAspect = 0.0;
    double meanAspect = 0.0;
    double maxGrade = 0.0; // largest neighbour size ratio along any axis
};

// Conforming trilinear-hexahedron mesh on a graded tensor grid with a cell mask (spec §6.1).
// Grid point (i, j, k) has coordinates (lines[0][i], lines[1][j], lines[2][k]); cell (i, j, k)
// spans [lines[0][i], lines[0][i+1]] × … . Linear indices run with i fastest.
struct HexMesh {
    std::array<std::vector<double>, 3> lines;
    std::vector<std::int32_t> cellBox;         // per grid cell: owning box, −1 = inactive
    std::vector<std::int32_t> boxMaterial;     // per box: material index (copied from the design)
    std::vector<std::int32_t> nodeOfGridPoint; // per grid point: node number, −1 = unused
    std::vector<std::array<double, 3>> nodeXyz;
    std::vector<std::array<std::int32_t, 3>> nodeGrid; // (i, j, k) per node: the coordinate hint
    std::vector<BoundaryFace> boundaryFaces;
    MeshStats stats;

    [[nodiscard]] std::int32_t cellsAlong(int axis) const noexcept;
    [[nodiscard]] std::int64_t cellIndex(std::int32_t i, std::int32_t j,
                                         std::int32_t k) const noexcept;
    [[nodiscard]] std::int64_t pointIndex(std::int32_t i, std::int32_t j,
                                          std::int32_t k) const noexcept;
    [[nodiscard]] std::array<std::int32_t, 3> cellIjk(std::int64_t cell) const noexcept;
    // Node numbers of an active cell in the usual hexahedron order: the bottom face (k) as
    // (i,j), (i+1,j), (i+1,j+1), (i,j+1), then the same four at k+1.
    [[nodiscard]] std::array<std::int32_t, 8> cellNodes(std::int64_t cell) const noexcept;
    // Cell edge lengths (dx, dy, dz) in metres.
    [[nodiscard]] std::array<double, 3> cellSize(std::int64_t cell) const noexcept;
    [[nodiscard]] double cellVolume(std::int64_t cell) const noexcept;
    [[nodiscard]] double faceArea(const BoundaryFace& face) const noexcept;
    // The four node numbers of a boundary face, counter-clockwise seen from outside.
    [[nodiscard]] std::array<std::int32_t, 4> faceNodes(const BoundaryFace& face) const noexcept;
};

// Grid lines along one axis (exposed for tests). `mandatory` holds every coordinate that must
// be a line (box faces); values closer than 1e-12 m are merged. `fixed` lists intervals whose
// gaps get exactly spec.fixedLayerCells equal cells. Every other gap is filled with cells no
// larger than s, graded so that neighbouring cells never differ by more than maxGradeRatio
// (build spec §3.4, amendment A5): equal cells where the neighbours allow it, geometric growth
// away from small neighbours otherwise. InvalidInput for bad spec values.
[[nodiscard]] Result<std::vector<double>>
buildAxis(std::vector<double> mandatory, const std::vector<std::pair<double, double>>& fixed,
          const MeshSpec& spec);

// Meshes a validated design. Errors: the design's own validation errors, InvalidInput for bad
// spec values, LimitExceeded when the grid would exceed MeshSpec::kMaxGridPoints or 2^31 nodes.
[[nodiscard]] Result<HexMesh> buildMesh(const Design& design, const MeshSpec& spec);

// Boundary surface as triangles (two per boundary face, outward normals), for rendering and
// STL export. Vertices are the mesh nodes on the boundary, renumbered compactly.
struct SurfaceMesh {
    std::vector<std::array<double, 3>> vertices;
    std::vector<std::int32_t> vertexNode; // mesh node number of each vertex
    std::vector<std::array<std::int32_t, 3>> triangles;
    std::vector<std::int32_t> triangleFace; // index into HexMesh::boundaryFaces
};
[[nodiscard]] SurfaceMesh extractSurface(const HexMesh& mesh);

} // namespace uniforge::fem3d
