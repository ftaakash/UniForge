#include "app/services/GeometryModel.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace uniforge {

namespace {

io::Vec3d centre(const io::Vec3d& a, const io::Vec3d& b) {
    return {(a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (a.z + b.z) * 0.5};
}

// Parsing reports [0, 1]; the model build takes the last 10 %.
ProgressFn scaled(const ProgressFn& progress, double share) {
    if (!progress) {
        return {};
    }
    return [progress, share](double f, std::string_view stage) { progress(f * share, stage); };
}

} // namespace

MeshModel buildMeshModel(io::StlMesh mesh, std::filesystem::path file) {
    MeshModel m;
    m.origin = centre(mesh.bbMin, mesh.bbMax);
    m.file = std::move(file);
    const std::size_t n = mesh.tri.size();
    m.positions.resize(n * 9);
    m.normals.resize(n * 9);
    for (std::size_t t = 0; t < n; ++t) {
        const io::Vec3d& nrm = mesh.normal[t];
        for (std::size_t v = 0; v < 3; ++v) {
            const io::Vec3d& p = mesh.tri[t][v];
            float* pos = &m.positions[t * 9 + v * 3];
            pos[0] = static_cast<float>(p.x - m.origin.x);
            pos[1] = static_cast<float>(p.y - m.origin.y);
            pos[2] = static_cast<float>(p.z - m.origin.z);
            float* nn = &m.normals[t * 9 + v * 3];
            nn[0] = static_cast<float>(nrm.x);
            nn[1] = static_cast<float>(nrm.y);
            nn[2] = static_cast<float>(nrm.z);
        }
    }
    m.source = std::move(mesh);
    return m;
}

PointModel buildPointModel(io::ThermalNodeSet nodes, std::filesystem::path file) {
    PointModel m;
    m.origin = centre(nodes.bbMin, nodes.bbMax);
    m.file = std::move(file);
    const std::size_t n = nodes.nodes.size();
    m.positions.resize(n * 3);
    m.values.resize(n);
    const double range = nodes.Tmax - nodes.Tmin;
    for (std::size_t i = 0; i < n; ++i) {
        const io::ThermalNode& node = nodes.nodes[i];
        m.positions[i * 3 + 0] = static_cast<float>(node.p.x - m.origin.x);
        m.positions[i * 3 + 1] = static_cast<float>(node.p.y - m.origin.y);
        m.positions[i * 3 + 2] = static_cast<float>(node.p.z - m.origin.z);
        m.values[i] = range > 0.0 ? static_cast<float>((node.T_C - nodes.Tmin) / range) : 0.5f;
    }
    m.source = std::move(nodes);
    return m;
}

Result<std::shared_ptr<const MeshModel>> importMeshModel(const std::filesystem::path& file,
                                                         const io::ImportLimits& limits,
                                                         const ProgressFn& progress,
                                                         const CancelToken* cancel) {
    auto parsed = io::parseAsciiStlFile(file, limits, scaled(progress, 0.9), cancel);
    if (!parsed) {
        return std::move(parsed).error();
    }
    if (progress) {
        progress(0.9, "Building render mesh");
    }
    auto model = std::make_shared<const MeshModel>(buildMeshModel(std::move(parsed).value(), file));
    if (cancel && cancel->cancelled()) {
        return makeError(ErrorCode::Cancelled, "Import cancelled.", "The previous model is kept.");
    }
    return std::shared_ptr<const MeshModel>(model);
}

Result<std::shared_ptr<const PointModel>> importPointModel(const std::filesystem::path& file,
                                                           const io::ImportLimits& limits,
                                                           const ProgressFn& progress,
                                                           const CancelToken* cancel) {
    auto parsed = io::parseThermalCsvFile(file, limits, scaled(progress, 0.9), cancel);
    if (!parsed) {
        return std::move(parsed).error();
    }
    if (progress) {
        progress(0.9, "Building point cloud");
    }
    auto model =
        std::make_shared<const PointModel>(buildPointModel(std::move(parsed).value(), file));
    if (cancel && cancel->cancelled()) {
        return makeError(ErrorCode::Cancelled, "Import cancelled.", "The previous data is kept.");
    }
    return std::shared_ptr<const PointModel>(model);
}

io::Vec3d sceneOrigin(const MeshModel* mesh, const PointModel* points) {
    io::BoundsAccumulator b;
    if (mesh) {
        b.add(mesh->source.bbMin);
        b.add(mesh->source.bbMax);
    }
    if (points) {
        b.add(points->source.bbMin);
        b.add(points->source.bbMax);
    }
    return b.empty() ? io::Vec3d{} : centre(b.min(), b.max());
}

} // namespace uniforge
