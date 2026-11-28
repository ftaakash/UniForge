// Q1 hexahedron and bilinear face matrices (build spec §3.3).

#include "core/fem3d/Element.hpp"

#include <cmath>

#include <gtest/gtest.h>

using namespace uniforge::fem3d;

TEST(Element, CornerNumbering) {
    EXPECT_EQ(hexCorner(0), (std::array<int, 3>{0, 0, 0}));
    EXPECT_EQ(hexCorner(2), (std::array<int, 3>{1, 1, 0}));
    EXPECT_EQ(hexCorner(3), (std::array<int, 3>{0, 1, 0}));
    EXPECT_EQ(hexCorner(5), (std::array<int, 3>{1, 0, 1}));
    EXPECT_EQ(hexCorner(7), (std::array<int, 3>{0, 1, 1}));
}

TEST(Element, CubeStiffnessHasTheTextbookEntries) {
    // Unit-conductivity cube of side h: diagonal h/3, edge neighbour 0, face diagonal −h/12,
    // body diagonal −h/12.
    const double h = 0.002;
    const Mat8 K = hexStiffness(1.0, h, h, h);
    EXPECT_NEAR(K[0][0], h / 3.0, 1e-18);
    EXPECT_NEAR(K[0][1], 0.0, 1e-18);       // edge (x)
    EXPECT_NEAR(K[0][2], -h / 12.0, 1e-18); // face diagonal (xy)
    EXPECT_NEAR(K[0][6], -h / 12.0, 1e-18); // body diagonal
    EXPECT_NEAR(K[0][4], 0.0, 1e-18);       // edge (z)
}

TEST(Element, StiffnessIsSymmetricWithZeroRowSumsAndRightEnergy) {
    const double k = 7.0, dx = 0.003, dy = 0.001, dz = 0.0005;
    const Mat8 K = hexStiffness(k, dx, dy, dz);
    for (std::size_t a = 0; a < 8; ++a) {
        double row = 0.0;
        for (std::size_t b = 0; b < 8; ++b) {
            EXPECT_DOUBLE_EQ(K[a][b], K[b][a]);
            row += K[a][b];
        }
        EXPECT_NEAR(row, 0.0, 1e-15 * K[a][a]) << "row " << a; // constant T: no heat flow
    }
    // T = x has gradient (1, 0, 0): Tᵀ K T = ∫ k |∇T|² dV = k·V.
    double energy = 0.0;
    for (std::size_t a = 0; a < 8; ++a) {
        for (std::size_t b = 0; b < 8; ++b) {
            const double ta = hexCorner(static_cast<int>(a))[0] * dx;
            const double tb = hexCorner(static_cast<int>(b))[0] * dx;
            energy += ta * K[a][b] * tb;
        }
    }
    EXPECT_NEAR(energy, k * dx * dy * dz, 1e-12 * k * dx * dy * dz);
}

TEST(Element, LoadsIntegrateTheSource) {
    const double q = 1e6, dx = 0.002, dy = 0.003, dz = 0.004;
    const Vec8 f = hexUniformLoad(q, dx, dy, dz);
    double sum = 0.0;
    for (const double v : f) {
        sum += v;
    }
    EXPECT_NEAR(sum, q * dx * dy * dz, 1e-15);
    double total = 0.0;
    const Vec8 g =
        hexLoad([q](double, double, double) { return q; }, {0.1, 0.2, 0.3}, dx, dy, dz, total);
    EXPECT_NEAR(total, q * dx * dy * dz, 1e-15);
    for (std::size_t a = 0; a < 8; ++a) {
        EXPECT_NEAR(g[a], f[a], 1e-15);
    }
    // A linear source puts more load on the nodes where it is larger.
    double t2 = 0.0;
    const Vec8 lin =
        hexLoad([](double x, double, double) { return x; }, {0.0, 0.0, 0.0}, 1.0, 1.0, 1.0, t2);
    EXPECT_NEAR(t2, 0.5, 1e-15);            // ∫ x dV over the unit cube
    EXPECT_NEAR(lin[1], 1.0 / 12.0, 1e-15); // ∫ x·N1 = (1/3)(1/4)
    EXPECT_NEAR(lin[0], 1.0 / 24.0, 1e-15); // ∫ x·N0 = (1/6)(1/4)
}

TEST(Element, FaceMatricesIntegrateTheFilm) {
    const double h = 25.0, area = 2e-6;
    const Mat4 H = faceConvection(h, area);
    double sum = 0.0;
    for (const auto& row : H) {
        for (const double v : row) {
            sum += v;
        }
    }
    EXPECT_NEAR(sum, h * area, 1e-18); // 1ᵀH1 = ∫ h dA
    EXPECT_NEAR(H[0][0], h * area / 9.0, 1e-18);
    EXPECT_NEAR(H[0][2], h * area / 36.0, 1e-18);

    const Vec4 g = faceUniformLoad(h * 25.0, area);
    const std::array<Point, 4> corners{
        {{0.0, 0.0, 0.0}, {0.002, 0.0, 0.0}, {0.002, 0.001, 0.0}, {0.0, 0.001, 0.0}}};
    const Vec4 gq = faceLoad(h, [](double, double, double) { return 25.0; }, corners);
    for (std::size_t a = 0; a < 4; ++a) {
        EXPECT_NEAR(gq[a], g[a], 1e-15);
    }
}

TEST(Element, NonCubicBoxesHavePositiveEdgeCouplings) {
    // K_01 couples the two nodes of an x-edge:
    //   K_01 = k·(−dy·dz/(9dx) + dx·dz/(18dy) + dx·dy/(18dz)).
    // It is 0 for a cube and positive as soon as the edge is longer than the other two (dy = dz):
    // trilinear hexahedra give positive off-diagonals, so K is not an M-matrix, for almost any
    // non-cubic cell (relevant to IC(0) breakdown, spec §5.3).
    EXPECT_NEAR(hexStiffness(1.0, 1.0, 1.0, 1.0)[0][1], 0.0, 1e-16);
    EXPECT_GT(hexStiffness(1.0, 1.1, 1.0, 1.0)[0][1], 0.0) << "x-edge 10 % longer";
    EXPECT_LT(hexStiffness(1.0, 0.9, 1.0, 1.0)[0][1], 0.0) << "x-edge 10 % shorter";
    const double dx = 1e-3, dy = 1e-3, dz = 1e-4; // a TIM cell: 1 mm × 1 mm × 0.1 mm
    const double expected = -dy * dz / (9 * dx) + dx * dz / (18 * dy) + dx * dy / (18 * dz);
    EXPECT_NEAR(hexStiffness(1.0, dx, dy, dz)[0][1], expected, 1e-18);
    EXPECT_GT(expected, 0.0);
}
