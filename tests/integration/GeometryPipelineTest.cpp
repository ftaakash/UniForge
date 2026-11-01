#include <algorithm>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "app/services/GeometryModel.hpp"
#include "app/services/ThermalStudy.hpp"

// Import → render-model pipeline without UI or GPU (spec §15 B1–B2): the committed samples load
// through the same functions the Geometry view's jobs call, and the float arrays are recentred.
// Also the Thermal view's study job and its CSV exports.
namespace uniforge {
namespace {

const std::filesystem::path kData = UNIFORGE_DATA_DIR;

TEST(GeometryPipeline, SampleStlBuildsRecentredFlatMesh) {
    auto r = importMeshModel(kData / "geometry" / "rod_10x10mm_1m.stl");
    ASSERT_TRUE(r.ok()) << r.error().message;
    const MeshModel& m = *r.value();
    EXPECT_EQ(m.positions.size(), m.source.tri.size() * 9);
    EXPECT_EQ(m.normals.size(), m.positions.size());
    float lo = 1e9f, hi = -1e9f;
    for (std::size_t i = 0; i < m.positions.size(); i += 3) {
        lo = std::min(lo, m.positions[i]);
        hi = std::max(hi, m.positions[i]);
    }
    EXPECT_NEAR(lo, -hi, 1e-6); // recentred: symmetric about 0
    EXPECT_NEAR(hi - lo, m.source.bbMax.x - m.source.bbMin.x, 1e-6);
    EXPECT_NEAR(m.origin.x, 0.5 * (m.source.bbMin.x + m.source.bbMax.x), 1e-12);
}

TEST(GeometryPipeline, SampleCsvBuildsNormalisedValues) {
    auto r = importPointModel(kData / "thermal" / "rod_nodal_temperature_SYNTHETIC.csv");
    ASSERT_TRUE(r.ok()) << r.error().message;
    const PointModel& p = *r.value();
    EXPECT_TRUE(p.source.syntheticFlag);
    ASSERT_EQ(p.values.size(), p.source.nodes.size());
    EXPECT_EQ(p.positions.size(), p.values.size() * 3);
    EXPECT_FLOAT_EQ(*std::min_element(p.values.begin(), p.values.end()), 0.0f);
    EXPECT_FLOAT_EQ(*std::max_element(p.values.begin(), p.values.end()), 1.0f);
}

TEST(GeometryPipeline, FarFromOriginKeepsMicrometreDetailInFloat) {
    io::StlMesh mesh;
    const double x0 = 1000.0; // 1 km from the origin, 10 µm triangle
    mesh.tri.push_back({io::Vec3d{x0, 0, 0}, io::Vec3d{x0 + 1e-5, 0, 0}, io::Vec3d{x0, 1e-5, 0}});
    mesh.normal.push_back({0, 0, 1});
    mesh.bbMin = {x0, 0, 0};
    mesh.bbMax = {x0 + 1e-5, 1e-5, 0};
    const MeshModel m = buildMeshModel(mesh, "far.stl");
    // Without recentring, float(1000.00001) == float(1000) and the triangle would collapse.
    EXPECT_NE(m.positions[0], m.positions[3]);
    EXPECT_NEAR(static_cast<double>(m.positions[3]) - m.positions[0], 1e-5, 1e-9);
}

TEST(GeometryPipeline, FailedImportReturnsTypedError) {
    auto r = importMeshModel(kData / "does_not_exist.stl");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::IoError);
    EXPECT_FALSE(r.error().message.empty());
}

TEST(GeometryPipeline, CancelledImportReportsCancelled) {
    CancelToken cancel;
    cancel.requestCancel();
    auto r = importPointModel(kData / "thermal" / "rod_nodal_temperature_SYNTHETIC.csv", {}, {},
                              &cancel);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::Cancelled);
}

TEST(GeometryPipeline, SceneOriginIsCentreOfUnion) {
    MeshModel a;
    a.source.bbMin = {0, 0, 0};
    a.source.bbMax = {1, 1, 1};
    PointModel b;
    b.source.bbMin = {2, 2, 2};
    b.source.bbMax = {3, 3, 3};
    EXPECT_DOUBLE_EQ(sceneOrigin(&a, &b).x, 1.5);
    EXPECT_DOUBLE_EQ(sceneOrigin(&a, nullptr).x, 0.5);
    EXPECT_DOUBLE_EQ(sceneOrigin(nullptr, nullptr).x, 0.0);
}

TEST(ThermalStudyJob, BriefStudyMatchesExpectedValuesAndExports) {
    auto r = runThermalStudy(thermal::RodProblem{}, {10, 20, 40, 80});
    ASSERT_TRUE(r.ok());
    const ThermalStudyResult& s = *r.value();
    ASSERT_EQ(s.rows.size(), 4u);
    ASSERT_EQ(s.meshes.size(), 4u);
    EXPECT_NEAR(s.rows[0].maxMidErr, 1.25, 1e-9);
    EXPECT_NEAR(s.rows[3].maxMidErr, 0.01953125, 1e-9);
    EXPECT_NEAR(s.rows[3].order, 2.0, 1e-6);
    EXPECT_NEAR(s.exactPeak.x, 0.42, 1e-12);
    EXPECT_NEAR(s.exactPeak.T, 188.2, 1e-9);
    EXPECT_EQ(s.exactX.size(), s.exactT.size());

    const auto dir = std::filesystem::temp_directory_path() / "uniforge_phase4_test";
    ASSERT_TRUE(exportConvergenceCsv(s, dir / "convergence.csv").ok());
    ASSERT_TRUE(exportNodesCsv(s, dir / "nodes.csv").ok());
    ASSERT_TRUE(exportMidpointsCsv(s, dir / "mid.csv").ok());
    std::string header;
    {
        std::ifstream in(dir / "convergence.csv");
        std::getline(in, header);
    }
    EXPECT_EQ(header, "N,h_m,max_mid_err_C,expected_qh2_8k_C,ratio,order,max_nodal_err_C,"
                      "energy_residual_W");
    std::filesystem::remove_all(dir);
}

TEST(ThermalStudyJob, InvalidProblemIsATypedError) {
    thermal::RodProblem p;
    p.k = -1.0;
    auto r = runThermalStudy(p, {10});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::InvalidInput);
}

} // namespace
} // namespace uniforge
