// Element matrices, mesh coordinates and global assembly (before and after Dirichlet BCs).

#include "core/thermal/ElementMatrices.hpp"

#include <cmath>
#include <cstddef>

#include <gtest/gtest.h>

#include "core/thermal/Assembler.hpp"
#include "core/thermal/DirichletBC.hpp"
#include "core/thermal/Mesh1D.hpp"

using namespace uniforge::thermal;

TEST(ElementMatrices, StiffnessIsKAOverHTimesPattern) {
    // Brief values, N = 10: kA/h = 10 · 1e-4 / 0.1 = 0.01 W/K.
    const Matrix2 ke = elementStiffness(10.0, 1e-4, 0.1);
    EXPECT_DOUBLE_EQ(ke[0][0], 0.01);
    EXPECT_DOUBLE_EQ(ke[0][1], -0.01);
    EXPECT_DOUBLE_EQ(ke[1][0], -0.01);
    EXPECT_DOUBLE_EQ(ke[1][1], 0.01);
}

TEST(ElementMatrices, ConsistentLoadSplitsHeatEqually) {
    // qAh/2 = 1e4 · 1e-4 · 0.1 / 2 = 0.05 W per node.
    const Vector2 fe = elementLoad(1e4, 1e-4, 0.1);
    EXPECT_DOUBLE_EQ(fe[0], 0.05);
    EXPECT_DOUBLE_EQ(fe[1], 0.05);
}

TEST(Mesh1D, NodesAreExactAtEndsAndRoundCoordinates) {
    const Mesh1D mesh{1.0, 50};
    EXPECT_EQ(mesh.nodeCount(), 51U);
    EXPECT_DOUBLE_EQ(mesh.h(), 0.02);
    EXPECT_EQ(mesh.node(0), 0.0);
    EXPECT_EQ(mesh.node(50), 1.0);
    EXPECT_EQ(mesh.node(21), 0.42) << "x = 0.42 m must be a node for N = 50";
}

TEST(Assembler, GlobalStiffnessIsSymmetricWithZeroRowSums) {
    const RodProblem p{}; // brief values, N = 10
    TridiagonalSystem s;
    assemble(p, s);
    ASSERT_EQ(s.size(), 11U);
    EXPECT_EQ(s.a[0], 0.0);
    EXPECT_EQ(s.c[10], 0.0);
    for (std::size_t i = 0; i + 1 < s.size(); ++i) {
        EXPECT_EQ(s.a[i + 1], s.c[i]) << "K must be symmetric at row " << i;
    }
    for (std::size_t i = 0; i < s.size(); ++i) {
        EXPECT_NEAR(s.a[i] + s.b[i] + s.c[i], 0.0, 1e-15) << "row " << i;
    }
    // Interior rows: [−s, 2s, −s]; end rows have one element.
    EXPECT_DOUBLE_EQ(s.b[0], 0.01);
    EXPECT_DOUBLE_EQ(s.b[5], 0.02);
    EXPECT_DOUBLE_EQ(s.b[10], 0.01);
    // Load: interior nodes get qAh, end nodes qAh/2; total = q·A·L = 1 W.
    EXPECT_DOUBLE_EQ(s.f[0], 0.05);
    EXPECT_DOUBLE_EQ(s.f[5], 0.1);
    double total = 0.0;
    for (const double v : s.f) {
        total += v;
    }
    EXPECT_NEAR(total, 1.0, 1e-15);
}

TEST(DirichletBC, EliminationKeepsSymmetryAndMakesIdentityRows) {
    const RodProblem p{};
    TridiagonalSystem s;
    assemble(p, s);
    const TridiagonalSystem before = s;
    const EliminatedRows rows = applyDirichlet(s, p.T0, p.TL);

    EXPECT_EQ(s.b[0], 1.0);
    EXPECT_EQ(s.c[0], 0.0);
    EXPECT_EQ(s.f[0], 100.0);
    EXPECT_EQ(s.b[10], 1.0);
    EXPECT_EQ(s.a[10], 0.0);
    EXPECT_EQ(s.f[10], 20.0);
    for (std::size_t i = 0; i + 1 < s.size(); ++i) {
        EXPECT_EQ(s.a[i + 1], s.c[i]) << "elimination must keep symmetry at row " << i;
    }
    // Known temperatures moved to the right-hand side of the neighbouring rows.
    EXPECT_DOUBLE_EQ(s.f[1], before.f[1] - before.a[1] * 100.0);
    EXPECT_DOUBLE_EQ(s.f[9], before.f[9] - before.c[9] * 20.0);
    // Diagonal dominance: weak in the interior, strict next to the boundary rows.
    for (std::size_t i = 1; i + 1 < s.size(); ++i) {
        EXPECT_GE(s.b[i], std::abs(s.a[i]) + std::abs(s.c[i]));
    }
    EXPECT_GT(s.b[1], std::abs(s.a[1]) + std::abs(s.c[1]));
    EXPECT_GT(s.b[9], std::abs(s.a[9]) + std::abs(s.c[9]));
    // The unconstrained boundary rows are returned for the reactions.
    EXPECT_EQ(rows.first.diag, before.b[0]);
    EXPECT_EQ(rows.first.upper, before.c[0]);
    EXPECT_EQ(rows.first.rhs, before.f[0]);
    EXPECT_EQ(rows.last.lower, before.a[10]);
    EXPECT_EQ(rows.last.diag, before.b[10]);
    EXPECT_EQ(rows.last.rhs, before.f[10]);
}
