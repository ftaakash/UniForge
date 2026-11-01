#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

#include "core/common/Result.hpp"
#include "core/io/StlParser.hpp"
#include "core/io/ThermalCsvParser.hpp"

namespace uniforge {

class JobContext;

// GPU-ready copy of an imported STL mesh (spec §5.3, "UI-only: RenderMesh"). Positions are
// recentred on the bounding-box centre in double precision *before* the float conversion, so
// a part far from the origin (e.g. x = 1000 m with 10 µm detail) keeps its detail in float.
// Built on the job thread; the renderer only uploads the arrays.
struct MeshModel {
    io::StlMesh source; // full-precision data (stats, bbox, name)
    std::filesystem::path file;
    io::Vec3d origin{};           // bbox centre, m: float position + origin = file position
    std::vector<float> positions; // 3 vertices per triangle, xyz, m (non-indexed: flat shading)
    std::vector<float> normals;   // facet normal repeated for each of its 3 vertices
};

// GPU-ready copy of an imported nodal-temperature CSV.
struct PointModel {
    io::ThermalNodeSet source; // full-precision data for the NodeInspector
    std::filesystem::path file;
    io::Vec3d origin{};           // bbox centre, m
    std::vector<float> positions; // xyz per node, recentred, m
    std::vector<float> values;    // (T − Tmin)/(Tmax − Tmin) per node in [0, 1]; 0.5 if flat
};

[[nodiscard]] MeshModel buildMeshModel(io::StlMesh mesh, std::filesystem::path file);
[[nodiscard]] PointModel buildPointModel(io::ThermalNodeSet nodes, std::filesystem::path file);

// Parse + build in one call, as run by the Geometry view's import jobs. Progress: parsing is
// 0–90 %, building the render arrays the rest. Cancelled imports return Error{Cancelled}.
[[nodiscard]] Result<std::shared_ptr<const MeshModel>>
importMeshModel(const std::filesystem::path& file, const io::ImportLimits& limits = {},
                const ProgressFn& progress = {}, const CancelToken* cancel = nullptr);
[[nodiscard]] Result<std::shared_ptr<const PointModel>>
importPointModel(const std::filesystem::path& file, const io::ImportLimits& limits = {},
                 const ProgressFn& progress = {}, const CancelToken* cancel = nullptr);

// Scene centre shared by mesh and points so both line up when drawn together: the centre of
// the union of the two bounding boxes (either may be null).
[[nodiscard]] io::Vec3d sceneOrigin(const MeshModel* mesh, const PointModel* points);

} // namespace uniforge
