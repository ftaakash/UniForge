// Shared vector and matrix–vector kernels (spec §5.5): values vs dense references and
// bit-identical results for every thread count (spec N2).

#include "core/sparse/VectorOps.hpp"

#include <cmath>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "RandomSparse.hpp"

using namespace uniforge::sparse;
using uniforge::testing::randomSparse;
using uniforge::testing::randomVector;

namespace {

const std::vector<int> kThreadCounts{1, 2, 3, 7, 20};

std::vector<double> denseProduct(const CsrMatrix& A, const std::vector<double>& x) {
    const std::vector<double> d = toDense(A);
    std::vector<double> y(static_cast<std::size_t>(A.rows), 0.0);
    for (std::size_t r = 0; r < y.size(); ++r) {
        for (std::size_t c = 0; c < x.size(); ++c) {
            y[r] += d[r * x.size() + c] * x[c];
        }
    }
    return y;
}

} // namespace

TEST(VectorOps, SmallValues) {
    KernelPool pool(2);
    const Vector x{1, -2, 3};
    const Vector y{4, 5, -6};
    EXPECT_EQ(dot(pool, x, y), 4.0 - 10.0 - 18.0);
    EXPECT_EQ(sum(pool, x), 2.0);
    EXPECT_EQ(norm2(pool, x), std::sqrt(14.0));
    EXPECT_EQ(normInf(pool, y), 6.0);
    EXPECT_EQ(maxAbsDiff(pool, x, y), 9.0);
    EXPECT_EQ(normInf(pool, Vector{}), 0.0);

    Vector z = y;
    axpy(pool, 2.0, x, z); // z = y + 2x
    EXPECT_EQ(z, (Vector{6, 1, 0}));
    z = y;
    xpay(pool, x, 0.5, z); // z = x + 0.5y
    EXPECT_EQ(z, (Vector{3, 0.5, 0}));
    multiplyPointwise(pool, x, y, z);
    EXPECT_EQ(z, (Vector{4, -10, -18}));
}

TEST(VectorOps, NormInfPropagatesNaN) {
    KernelPool pool(3);
    Vector x(static_cast<std::size_t>(3 * KernelPool::kChunkRows), 1.0);
    x[9000] = std::numeric_limits<double>::quiet_NaN();
    EXPECT_TRUE(std::isnan(normInf(pool, x)));
    EXPECT_TRUE(std::isnan(maxAbsDiff(pool, x, Vector(x.size(), 0.0))));
}

TEST(VectorOps, SpmvAndResidualMatchDense) {
    KernelPool pool(4);
    const CsrMatrix A = randomSparse(60, 45, 5, 7);
    const Vector x = randomVector(45, 8);
    const Vector b = randomVector(60, 9);
    const Vector expected = denseProduct(A, x);

    Vector y(60);
    spmv(pool, A, x, y);
    Vector r(60);
    residual(pool, A, x, b, r);
    for (std::size_t i = 0; i < y.size(); ++i) {
        EXPECT_NEAR(y[i], expected[i], 1e-9 * (1.0 + std::fabs(expected[i])));
        EXPECT_NEAR(r[i], b[i] - expected[i], 1e-9 * (1.0 + std::fabs(b[i] - expected[i])));
    }
}

TEST(VectorOps, DiagonalOfSquareMatrix) {
    auto A = fromTriplets(3, 3, {{0, 0, 2}, {1, 2, 5}, {2, 2, 7}});
    ASSERT_TRUE(A.ok());
    EXPECT_EQ(diagonal(A.value()), (Vector{2, 0, 7}));
}

TEST(VectorOps, EveryKernelIsBitIdenticalForEveryThreadCount) {
    const Index n = 9 * KernelPool::kChunkRows + 311;
    const CsrMatrix A = randomSparse(n, n, 7, 11, /*symmetric=*/true);
    const Vector x = randomVector(n, 12);
    const Vector y = randomVector(n, 13);

    struct Results {
        double dot, sum, norm2, normInf, diff;
        Vector axpy, xpay, spmv, residual;
    };
    const auto runAll = [&](int p) {
        KernelPool pool(p);
        Results out;
        out.dot = dot(pool, x, y);
        out.sum = sum(pool, x);
        out.norm2 = norm2(pool, x);
        out.normInf = normInf(pool, x);
        out.diff = maxAbsDiff(pool, x, y);
        out.axpy = y;
        axpy(pool, 0.3, x, out.axpy);
        out.xpay = y;
        xpay(pool, x, -1.7, out.xpay);
        out.spmv.resize(x.size());
        spmv(pool, A, x, out.spmv);
        out.residual.resize(x.size());
        residual(pool, A, x, y, out.residual);
        return out;
    };

    const Results ref = runAll(1);
    for (const int p : kThreadCounts) {
        const Results got = runAll(p);
        EXPECT_EQ(got.dot, ref.dot) << "p = " << p;
        EXPECT_EQ(got.sum, ref.sum) << "p = " << p;
        EXPECT_EQ(got.norm2, ref.norm2) << "p = " << p;
        EXPECT_EQ(got.normInf, ref.normInf) << "p = " << p;
        EXPECT_EQ(got.diff, ref.diff) << "p = " << p;
        EXPECT_EQ(got.axpy, ref.axpy) << "p = " << p;
        EXPECT_EQ(got.xpay, ref.xpay) << "p = " << p;
        EXPECT_EQ(got.spmv, ref.spmv) << "p = " << p;
        EXPECT_EQ(got.residual, ref.residual) << "p = " << p;
    }
}
