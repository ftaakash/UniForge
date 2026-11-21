// CSR assembly with convection and symmetric Dirichlet elimination (build spec §3.5).

#include "core/fem3d/Assembly.hpp"

#include <cmath>
#include <utility>

#include <gtest/gtest.h>

#include "core/fem3d/Post.hpp"

using namespace uniforge::fem3d;
using uniforge::ErrorCode;
using uniforge::sparse::Index;
using uniforge::sparse::Offset;
using uniforge::sparse::toSize;

namespace {

HexMesh meshOf(const Design& d, double s) {
    MeshSpec spec;
    spec.targetSpacing = s;
    auto m = buildMesh(d, spec);
    EXPECT_TRUE(m.ok());
    return std::move(m).value();
}

LinearSystem assembled(const Design& d, const HexMesh& m, AssemblyOptions o = {}) {
    auto s = assemble(d, m, o);
    EXPECT_TRUE(s.ok()) << (s.ok() ? "" : s.error().message);
    return std::move(s).value();
}

double rowSum(const CsrMatrix& K, Index r) {
    double s = 0.0;
    for (Offset k = K.rowPtr[toSize(r)]; k < K.rowPtr[toSize(r) + 1]; ++k) {
        s += K.val[toSize(k)];
    }
    return s;
}

} // namespace

TEST(Assembly, ConductionMatrixIsSymmetricWithZeroRowSums) {
    // Raw bar system (Dirichlet not applied, no convection): rows sum to 0, loads to q·V = 1 W.
    const Design bar = makeV1Bar();
    const HexMesh m = meshOf(bar, 0.01);
    AssemblyOptions o;
    o.applyDirichlet = false;
    const LinearSystem s = assembled(bar, m, o);
    ASSERT_TRUE(uniforge::sparse::validate(s.K).ok());
    EXPECT_TRUE(isSymmetric(s.K, 1e-14));
    for (Index r = 0; r < s.K.rows; ++r) {
        ASSERT_NEAR(rowSum(s.K, r), 0.0, 1e-12 * s.K.at(r, r)) << "row " << r;
    }
    double load = 0.0;
    for (const double v : s.f) {
        load += v;
    }
    EXPECT_NEAR(load, 1.0, 1e-12);
    EXPECT_NEAR(s.heatIn, 1.0, 1e-12);
    EXPECT_EQ(s.dirichletNodes.size(), 8U); // 4 nodes on each end
    EXPECT_GT(s.matrixBytes, 0U);
}

TEST(Assembly, InteriorNodeCouplesWith27Nodes) {
    const Design cube = makeCube();
    const HexMesh m = meshOf(cube, cube.boxes[0].max[0] / 3.0); // 3 × 3 × 3 cells
    const LinearSystem s = assembled(cube, m);
    std::size_t maxRow = 0;
    for (Index r = 0; r < s.K.rows; ++r) {
        maxRow = std::max(maxRow, toSize(s.K.rowPtr[toSize(r) + 1] - s.K.rowPtr[toSize(r)]));
    }
    EXPECT_EQ(maxRow, 27U);
}

TEST(Assembly, ConvectionAddsTheFilmToTheRowSums) {
    // Plate (base + die), no Dirichlet: 1ᵀK1 = Σ h·A over convective faces, 1ᵀf = P + Σ h·T∞·A.
    const Design plate = makePlate();
    const HexMesh m = meshOf(plate, 2e-3);
    const LinearSystem s = assembled(plate, m);
    double total = 0.0;
    for (Index r = 0; r < s.K.rows; ++r) {
        total += rowSum(s.K, r);
    }
    // Plate convective area: top 60·40 + sides 2·60·4 + 2·40·4 = 3200 mm²; h = 25.
    const double hA = 25.0 * 3200e-6;
    EXPECT_NEAR(total, hA, 1e-12);
    double load = 0.0;
    for (const double v : s.f) {
        load += v;
    }
    EXPECT_NEAR(load, 30.0 + hA * 25.0, 1e-9);
    EXPECT_TRUE(s.dirichletNodes.empty());
    EXPECT_TRUE(isSymmetric(s.K, 1e-14));
}

TEST(Assembly, DirichletEliminationIsSymmetricWithIdentityRows) {
    const Design bar = makeV1Bar();
    const HexMesh m = meshOf(bar, 0.01);
    const LinearSystem s = assembled(bar, m);
    EXPECT_TRUE(isSymmetric(s.K, 0.0));
    ASSERT_EQ(s.dirichletNodes.size(), 8U);
    for (std::size_t i = 0; i < s.dirichletNodes.size(); ++i) {
        const Index d = s.dirichletNodes[i];
        EXPECT_EQ(s.freeMask[toSize(d)], 0);
        EXPECT_EQ(s.K.at(d, d), 1.0);
        EXPECT_EQ(rowSum(s.K, d), 1.0) << "identity row";
        EXPECT_EQ(s.f[toSize(d)], s.dirichletValues[i]);
        const double x = m.nodeXyz[toSize(d)][0];
        EXPECT_EQ(s.dirichletValues[i], x == 0.0 ? 100.0 : 20.0);
    }
    EXPECT_EQ(s.dirichletRows.rows, 8);
    EXPECT_EQ(s.dirichletLoad.size(), 8U);
}

TEST(Assembly, IsDeterministic) {
    const Design hs = makeHS7();
    const HexMesh m = meshOf(hs, 2e-3);
    const LinearSystem a = assembled(hs, m);
    const LinearSystem b = assembled(hs, m);
    EXPECT_EQ(a.K.val, b.K.val);
    EXPECT_EQ(a.f, b.f);
}

TEST(Assembly, RejectsConflictingDirichletValuesAndForeignMeshes) {
    Design bar = makeV1Bar();
    bar.boxes[0].faces[YMin] = FaceBc::dirichlet(50.0); // shares edges with the 100 °C end
    const HexMesh m = meshOf(bar, 0.01);
    EXPECT_EQ(assemble(bar, m).error().code, ErrorCode::InvalidInput);

    const HexMesh other = meshOf(makeHS7(), 2e-3);
    EXPECT_EQ(assemble(makeV1Bar(), other).error().code, ErrorCode::InvalidInput);
}

TEST(Assembly, SamplingAndPeakOnAKnownField) {
    const Design cube = makeCube();
    const HexMesh m = meshOf(cube, 0.01);
    Vector T(m.nodeXyz.size());
    for (std::size_t i = 0; i < T.size(); ++i) {
        const auto& p = m.nodeXyz[i];
        T[i] = 1.0 + 100.0 * p[0] - 50.0 * p[1] + 20.0 * p[2]; // trilinear: interpolated exactly
    }
    auto v = sampleTemperature(m, T, 0.0123, -0.004, 0.0071);
    ASSERT_TRUE(v.ok());
    EXPECT_NEAR(v.value(), 1.0 + 1.23 + 0.2 + 0.142, 1e-12);
    EXPECT_EQ(sampleTemperature(m, T, 1.0, 0.0, 0.0).error().code, ErrorCode::InvalidInput);
    const Peak p = peakTemperature(m, T);
    EXPECT_NEAR(p.at[0], cube.boxes[0].max[0], 1e-15);
    const ErrorNorms e = errorNorms(
        m, T, [](double x, double y, double z) { return 1.0 + 100.0 * x - 50.0 * y + 20.0 * z; });
    EXPECT_LT(e.l2, 1e-12);
    EXPECT_LT(e.maxNodal, 1e-12);
    EXPECT_NEAR(e.volume, cube.volume(), 1e-15);
}
