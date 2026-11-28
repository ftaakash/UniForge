// Graded tensor-grid hex mesher (build spec §3.4, amendments A1/A5): grid lines, exact volume
// and areas, watertight outward surface, grading, STL round trip through the v1 importer.

#include "core/fem3d/HexMesh.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/io/StlParser.hpp"
#include "core/io/StlWriter.hpp"

using namespace uniforge::fem3d;
using uniforge::ErrorCode;

namespace {

constexpr double kR = 1.5;

HexMesh meshOf(const Design& d, double s, int layerCells = 1) {
    MeshSpec spec;
    spec.targetSpacing = s;
    spec.fixedLayerCells = layerCells;
    auto m = buildMesh(d, spec);
    EXPECT_TRUE(m.ok()) << (m.ok() ? "" : m.error().message);
    return std::move(m).value();
}

double activeVolume(const HexMesh& m) {
    double v = 0.0;
    for (std::int64_t c = 0; c < static_cast<std::int64_t>(m.cellBox.size()); ++c) {
        if (m.cellBox[static_cast<std::size_t>(c)] >= 0) {
            v += m.cellVolume(c);
        }
    }
    return v;
}

// Σ area of boundary faces whose boundary condition has the given kind.
double areaOf(const HexMesh& m, const Design& d, BcKind kind) {
    double a = 0.0;
    for (const BoundaryFace& f : m.boundaryFaces) {
        if (d.boxes[static_cast<std::size_t>(f.box)].faces[static_cast<std::size_t>(f.side)].kind ==
            kind) {
            a += m.faceArea(f);
        }
    }
    return a;
}

void expectGraded(const std::vector<double>& lines, double s, const std::string& what) {
    for (std::size_t c = 0; c + 1 < lines.size(); ++c) {
        const double h = lines[c + 1] - lines[c];
        ASSERT_GT(h, 0.0) << what << " cell " << c;
        ASSERT_LE(h, s * (1.0 + 1e-12)) << what << " cell " << c;
        if (c > 0) {
            const double prev = lines[c] - lines[c - 1];
            ASSERT_LE(std::max(h / prev, prev / h), kR * (1.0 + 1e-9)) << what << " cell " << c;
        }
    }
}

} // namespace

// ---- Axis -----------------------------------------------------------------------------------

TEST(MeshAxis, UniformGapGetsCeilOfLengthOverSpacingEqualCells) {
    MeshSpec spec;
    spec.targetSpacing = 1e-3;
    auto r = buildAxis({0.0, 4.5e-3}, {}, spec);
    ASSERT_TRUE(r.ok());
    const auto& l = r.value();
    ASSERT_EQ(l.size(), 6U); // ceil(4.5) = 5 cells
    for (std::size_t c = 0; c + 1 < l.size(); ++c) {
        EXPECT_NEAR(l[c + 1] - l[c], 0.9e-3, 1e-15);
    }
    EXPECT_EQ(l.back(), 4.5e-3); // mandatory lines are exact
}

TEST(MeshAxis, MandatoryLinesAreKeptAndNearDuplicatesMerged) {
    MeshSpec spec;
    spec.targetSpacing = 2e-3;
    auto r = buildAxis({0.003, 0.0, 0.003 + 1e-15, 0.010, 0.0}, {}, spec);
    ASSERT_TRUE(r.ok());
    const auto& l = r.value();
    EXPECT_TRUE(std::find(l.begin(), l.end(), 0.003) != l.end());
    EXPECT_TRUE(std::is_sorted(l.begin(), l.end()));
    EXPECT_EQ(l.front(), 0.0);
    EXPECT_EQ(l.back(), 0.010);
}

TEST(MeshAxis, ThinLayerIsGradedIntoItsNeighbours) {
    // 0.1 mm layer between a 0.5 mm and a 30 mm gap, s = 1 mm: the neighbours must start near
    // 0.15 mm and grow by at most 1.5 per cell.
    MeshSpec spec;
    spec.targetSpacing = 1e-3;
    auto r = buildAxis({-0.6e-3, -0.1e-3, 0.0, 0.030}, {}, spec);
    ASSERT_TRUE(r.ok());
    expectGraded(r.value(), 1e-3, "thin layer");
    // The layer itself is one cell of 0.1 mm.
    const auto& l = r.value();
    const auto it = std::find(l.begin(), l.end(), -0.1e-3);
    ASSERT_NE(it, l.end());
    EXPECT_EQ(*(it + 1), 0.0);
}

TEST(MeshAxis, FixedLayerGetsTheRequestedCellCount) {
    MeshSpec spec;
    spec.targetSpacing = 1e-3;
    spec.fixedLayerCells = 2;
    auto r = buildAxis({-0.6e-3, -0.1e-3, 0.0, 0.004}, {{-0.1e-3, 0.0}}, spec);
    ASSERT_TRUE(r.ok());
    const auto& l = r.value();
    const auto it = std::find(l.begin(), l.end(), -0.1e-3);
    ASSERT_NE(it, l.end());
    EXPECT_NEAR(*(it + 1), -0.05e-3, 1e-18);
    EXPECT_EQ(*(it + 2), 0.0);
    expectGraded(l, 1e-3, "fixed layer");
}

TEST(MeshAxis, RejectsBadSpecs) {
    MeshSpec spec;
    spec.targetSpacing = 0.0;
    EXPECT_EQ(buildAxis({0.0, 1.0}, {}, spec).error().code, ErrorCode::InvalidInput);
    spec = {};
    spec.maxGradeRatio = 1.0;
    EXPECT_EQ(buildAxis({0.0, 1.0}, {}, spec).error().code, ErrorCode::InvalidInput);
    spec = {};
    spec.fixedLayerCells = 0;
    EXPECT_EQ(buildAxis({0.0, 1.0}, {}, spec).error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(buildAxis({1.0, 1.0}, {}, MeshSpec{}).error().code, ErrorCode::InvalidInput);
}

// ---- Mesh -----------------------------------------------------------------------------------

TEST(HexMesh, BarCountsMatchHandCalculation) {
    // 1 m bar at s = 10 mm: 100 × 1 × 1 cells, 101 × 2 × 2 nodes, 2 end faces + 4 × 100 sides.
    const HexMesh m = meshOf(makeV1Bar(), 0.010);
    EXPECT_EQ(m.stats.activeCells, 100);
    EXPECT_EQ(m.stats.nodes, 404);
    EXPECT_EQ(m.boundaryFaces.size(), 402U);
    EXPECT_NEAR(m.stats.maxAspect, 1.0, 1e-9);
    EXPECT_NEAR(m.stats.maxGrade, 1.0, 1e-9); // equal cells, up to rounding
    const Design bar = makeV1Bar();
    EXPECT_NEAR(areaOf(m, bar, BcKind::Dirichlet), 2e-4, 1e-18);
    EXPECT_NEAR(areaOf(m, bar, BcKind::Insulated), 4 * 1.0 * 0.01, 1e-15);
}

TEST(HexMesh, VolumeIsExactForEveryLadderObject) {
    for (const Design& d :
         {makeV1Bar(), makeCube(), makePlate(), makeFinnedPlate(), makeHS7(), makePinFin()}) {
        const double s = d.ladderLevel == "L0a" ? 2e-3 : 1e-3;
        const HexMesh m = meshOf(d, s);
        // Exact up to rounding of the line coordinates (~1e-12 relative after ~1e5 cells).
        EXPECT_NEAR(activeVolume(m), d.volume(), 1e-10 * d.volume()) << d.name;
        EXPECT_GT(m.stats.nodes, 0) << d.name;
    }
}

TEST(HexMesh, HeatSinkConvectiveAreaIsExact) {
    // Hand calculation for HS-7 (mm²): base top 60·40 − 7·2·40 = 1840; base sides
    // 2·60·4 + 2·40·4 = 800; each fin 2·40·26 + 2·40 + 2·2·26 = 2264, × 7 = 15848.
    const double expected = (1840.0 + 800.0 + 15848.0) * 1e-6;
    const Design hs = makeHS7();
    for (const double s : {1e-3, 0.63e-3}) {
        const HexMesh m = meshOf(hs, s);
        EXPECT_NEAR(areaOf(m, hs, BcKind::Convection), expected, 1e-12 * expected) << "s = " << s;
    }
}

TEST(HexMesh, SurfaceIsWatertightWithOutwardNormals) {
    for (const Design& d : {makeHS7(), makePinFin(), makeCube()}) {
        const HexMesh m = meshOf(d, 1e-3);
        // Every boundary edge is shared by exactly two boundary faces.
        std::map<std::pair<std::int32_t, std::int32_t>, int> edges;
        for (const BoundaryFace& f : m.boundaryFaces) {
            const auto n = m.faceNodes(f);
            for (std::size_t e = 0; e < 4; ++e) {
                const std::int32_t a = n[e];
                const std::int32_t b = n[(e + 1) % 4];
                ++edges[{std::min(a, b), std::max(a, b)}];
            }
        }
        for (const auto& [edge, count] : edges) {
            ASSERT_EQ(count, 2) << d.name << ": edge " << edge.first << "–" << edge.second;
        }
        // Each face normal (from the node order) points away from its cell's centre.
        for (const BoundaryFace& f : m.boundaryFaces) {
            const auto n = m.faceNodes(f);
            const auto& p0 = m.nodeXyz[static_cast<std::size_t>(n[0])];
            const auto& p1 = m.nodeXyz[static_cast<std::size_t>(n[1])];
            const auto& p2 = m.nodeXyz[static_cast<std::size_t>(n[2])];
            const std::array<double, 3> u{p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
            const std::array<double, 3> v{p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
            const std::array<double, 3> normal{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                                               u[0] * v[1] - u[1] * v[0]};
            const auto cn = m.cellNodes(f.cell);
            std::array<double, 3> centre{};
            for (const auto node : cn) {
                for (std::size_t a = 0; a < 3; ++a) {
                    centre[a] += m.nodeXyz[static_cast<std::size_t>(node)][a] / 8.0;
                }
            }
            double outward = 0.0;
            for (std::size_t a = 0; a < 3; ++a) {
                outward += normal[a] * (p0[a] - centre[a]);
            }
            ASSERT_GT(outward, 0.0) << d.name << ": side " << f.side;
        }
    }
}

TEST(HexMesh, GradingStaysWithinTheRatioAndSpacingAcrossTheLadder) {
    for (const double s : {1e-3, 0.5e-3}) {
        for (const int layerCells : {1, 2}) {
            const HexMesh m = meshOf(makeHS7(), s, layerCells);
            for (std::size_t a = 0; a < 3; ++a) {
                expectGraded(m.lines[a], s, "HS-7 axis " + std::to_string(a));
            }
            EXPECT_LE(m.stats.maxGrade, kR * (1.0 + 1e-9));
            EXPECT_GE(m.stats.maxAspect, m.stats.meanAspect);
            EXPECT_GE(m.stats.meanAspect, m.stats.minAspect);
            EXPECT_GE(m.stats.minAspect, 1.0);
        }
    }
}

TEST(HexMesh, RefinementRoughlyDoublesNodesPerStep) {
    // Geometric ladder s_k = 1 mm · 2^(−k/3) (amendment A1): DOF should about double per step.
    std::int64_t previous = 0;
    for (int k = 0; k <= 3; ++k) {
        const double s = 1e-3 * std::pow(2.0, -k / 3.0);
        const HexMesh m = meshOf(makeHS7(), s);
        if (previous > 0) {
            const double ratio = static_cast<double>(m.stats.nodes) / static_cast<double>(previous);
            EXPECT_GT(ratio, 1.5) << "k = " << k;
            EXPECT_LT(ratio, 2.8) << "k = " << k;
        }
        previous = m.stats.nodes;
    }
}

TEST(HexMesh, EveryNodeBelongsToAnActiveCellAndCoordinatesMatchTheGrid) {
    const HexMesh m = meshOf(makeHS7(), 1e-3);
    std::vector<int> used(m.nodeXyz.size(), 0);
    for (std::int64_t c = 0; c < static_cast<std::int64_t>(m.cellBox.size()); ++c) {
        if (m.cellBox[static_cast<std::size_t>(c)] < 0) {
            continue;
        }
        for (const auto n : m.cellNodes(c)) {
            ASSERT_GE(n, 0);
            used[static_cast<std::size_t>(n)] = 1;
        }
    }
    for (std::size_t n = 0; n < used.size(); ++n) {
        ASSERT_EQ(used[n], 1) << "node " << n;
        const auto& g = m.nodeGrid[n];
        for (std::size_t a = 0; a < 3; ++a) {
            ASSERT_EQ(m.nodeXyz[n][a], m.lines[a][static_cast<std::size_t>(g[a])]);
        }
    }
}

TEST(HexMesh, MaterialsFollowTheBoxes) {
    const Design hs = makeHS7();
    const HexMesh m = meshOf(hs, 1e-3);
    double timVolume = 0.0;
    for (std::int64_t c = 0; c < static_cast<std::int64_t>(m.cellBox.size()); ++c) {
        const auto box = m.cellBox[static_cast<std::size_t>(c)];
        if (box >= 0 && m.boxMaterial[static_cast<std::size_t>(box)] == 2) {
            timVolume += m.cellVolume(c);
        }
    }
    EXPECT_NEAR(timVolume, 0.020 * 0.020 * 1e-4, 1e-18);
}

TEST(HexMesh, RefusesAbsurdSpacings) {
    MeshSpec spec;
    spec.targetSpacing = 1e-6; // 1 m bar at 1 µm
    EXPECT_EQ(buildMesh(makeV1Bar(), spec).error().code, ErrorCode::LimitExceeded);
    spec.targetSpacing = -1.0;
    EXPECT_EQ(buildMesh(makeV1Bar(), spec).error().code, ErrorCode::InvalidInput);
    Design broken = makeV1Bar();
    broken.materials[0].k = -1.0;
    EXPECT_EQ(buildMesh(broken, MeshSpec{}).error().code, ErrorCode::InvalidInput);
}

// ---- Surface and STL ------------------------------------------------------------------------

TEST(HexMesh, SurfaceStlRoundTripsThroughTheV1Importer) {
    const Design hs = makeHS7();
    const HexMesh m = meshOf(hs, 2e-3);
    const SurfaceMesh s = extractSurface(m);
    ASSERT_EQ(s.triangles.size(), 2 * m.boundaryFaces.size());

    std::string stl;
    uniforge::io::appendStlBegin(stl, "hs7");
    double signedVolume = 0.0; // divergence theorem: Σ (p0 · (p1 × p2)) / 6 over a closed surface
    for (const auto& t : s.triangles) {
        std::array<uniforge::io::Vec3d, 3> v{};
        for (std::size_t i = 0; i < 3; ++i) {
            const auto& p = s.vertices[static_cast<std::size_t>(t[i])];
            v[i] = {p[0], p[1], p[2]};
        }
        uniforge::io::appendStlFacet(stl, v);
        signedVolume += (v[0].x * (v[1].y * v[2].z - v[1].z * v[2].y) -
                         v[0].y * (v[1].x * v[2].z - v[1].z * v[2].x) +
                         v[0].z * (v[1].x * v[2].y - v[1].y * v[2].x)) /
                        6.0;
    }
    uniforge::io::appendStlEnd(stl, "hs7");
    EXPECT_NEAR(signedVolume, hs.volume(), 1e-9 * hs.volume()) << "closed, outward surface";

    std::istringstream in(stl);
    auto parsed = uniforge::io::parseAsciiStl(in, stl.size());
    ASSERT_TRUE(parsed.ok()) << parsed.error().message;
    EXPECT_EQ(parsed.value().tri.size(), s.triangles.size());
    EXPECT_EQ(parsed.value().degenerate, 0U);
    EXPECT_NEAR(parsed.value().bbMin.x, 0.0, 1e-15);
    EXPECT_NEAR(parsed.value().bbMax.x, 0.060, 1e-15);
    EXPECT_NEAR(parsed.value().bbMin.z, -0.0006, 1e-15);
    EXPECT_NEAR(parsed.value().bbMax.z, 0.030, 1e-15);
}
