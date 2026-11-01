// The committed sample files in data/ import cleanly and match their documented content
// (data/PROVENANCE.md): the Task C rod and the SYNTHETIC nodal temperatures on it.

#include <cmath>
#include <filesystem>

#include <gtest/gtest.h>

#include "core/io/StlParser.hpp"
#include "core/io/ThermalCsvParser.hpp"
#include "core/thermal/Analytical.hpp"

using namespace uniforge::io;

namespace {

std::filesystem::path sample(const char* relative) {
    return std::filesystem::path(UNIFORGE_DATA_DIR) / relative;
}

} // namespace

TEST(SampleData, RodStlIsTheTaskCRod) {
    const auto r = parseAsciiStlFile(sample("geometry/rod_10x10mm_1m.stl"));
    ASSERT_TRUE(r.ok()) << r.error().message;
    const StlMesh& m = r.value();
    EXPECT_EQ(m.name, "rod_10x10mm_1m");
    EXPECT_EQ(m.tri.size(), 12U);
    EXPECT_EQ(m.degenerate, 0U);
    EXPECT_TRUE(m.stats.warnings.empty());
    const uniforge::thermal::RodProblem rod;
    EXPECT_EQ(m.bbMax.x - m.bbMin.x, rod.L);
    EXPECT_EQ((m.bbMax.y - m.bbMin.y) * (m.bbMax.z - m.bbMin.z), rod.A); // 10 mm x 10 mm
}

TEST(SampleData, HeatsinkStlImports) {
    const auto r = parseAsciiStlFile(sample("geometry/heatsink_fins.stl"));
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().tri.size(), 96U);
    EXPECT_EQ(r.value().degenerate, 0U);
    EXPECT_DOUBLE_EQ(r.value().bbMax.z, 0.03);
}

TEST(SampleData, SyntheticCsvIsFlaggedAndFollowsTheRodSolution) {
    const auto r = parseThermalCsvFile(sample("thermal/rod_nodal_temperature_SYNTHETIC.csv"));
    ASSERT_TRUE(r.ok()) << r.error().message;
    const ThermalNodeSet& s = r.value();
    EXPECT_TRUE(s.syntheticFlag);
    EXPECT_EQ(s.nodes.size(), 909U);
    EXPECT_EQ(s.bbMin.x, 0.0);
    EXPECT_EQ(s.bbMax.x, 1.0);
    // Centre-line nodes (y = z = 0) carry T_exact(x) plus at most 0.02 °C noise (+ rounding).
    const uniforge::thermal::RodProblem rod;
    int centre = 0;
    for (const ThermalNode& n : s.nodes) {
        if (n.p.y == 0.0 && n.p.z == 0.0) {
            ++centre;
            EXPECT_LE(std::abs(n.T_C - uniforge::thermal::exactTemperature(rod, n.p.x)), 0.0201)
                << "node " << n.id;
        }
    }
    EXPECT_EQ(centre, 101);
    EXPECT_NEAR(s.Tmax, 188.2, 0.0201);
}
