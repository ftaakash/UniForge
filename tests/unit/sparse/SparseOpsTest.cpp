// Transpose and sparse matrix–matrix product (spec §5.4/§5.5, used by the AMG Galerkin product).

#include "core/sparse/SparseOps.hpp"

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "RandomSparse.hpp"
#include "core/sparse/KernelPool.hpp"

using namespace uniforge::sparse;
using uniforge::ErrorCode;
using uniforge::testing::randomSparse;

namespace {

std::vector<double> denseMultiply(const CsrMatrix& A, const CsrMatrix& B) {
    const auto a = toDense(A);
    const auto b = toDense(B);
    const std::size_t m = toSize(A.rows), k = toSize(A.cols), n = toSize(B.cols);
    std::vector<double> c(m * n, 0.0);
    for (std::size_t i = 0; i < m; ++i) {
        for (std::size_t l = 0; l < k; ++l) {
            for (std::size_t j = 0; j < n; ++j) {
                c[i * n + j] += a[i * k + l] * b[l * n + j];
            }
        }
    }
    return c;
}

void expectClose(const std::vector<double>& got, const std::vector<double>& want) {
    ASSERT_EQ(got.size(), want.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
        ASSERT_NEAR(got[i], want[i], 1e-12 * (1.0 + std::fabs(want[i]))) << "entry " << i;
    }
}

} // namespace

TEST(SparseOps, TransposeMatchesDenseAndIsValid) {
    const CsrMatrix A = randomSparse(23, 41, 4, 101);
    const CsrMatrix At = transpose(A);
    ASSERT_TRUE(validate(At).ok()) << "columns sorted, sizes consistent";
    EXPECT_EQ(At.rows, 41);
    EXPECT_EQ(At.cols, 23);
    const auto a = toDense(A);
    const auto at = toDense(At);
    for (std::size_t r = 0; r < 23; ++r) {
        for (std::size_t c = 0; c < 41; ++c) {
            ASSERT_EQ(at[c * 23 + r], a[r * 41 + c]);
        }
    }
    const CsrMatrix Att = transpose(At);
    EXPECT_EQ(Att.rowPtr, A.rowPtr);
    EXPECT_EQ(Att.col, A.col);
    EXPECT_EQ(Att.val, A.val);
}

TEST(SparseOps, MultiplyMatchesDenseIncludingEmptyRows) {
    CsrMatrix A = randomSparse(30, 20, 3, 102);
    // Clear row 4 of A: an empty row must give an empty row of C.
    {
        std::vector<Triplet> t;
        for (Index r = 0; r < A.rows; ++r) {
            for (Offset k = A.rowPtr[toSize(r)]; k < A.rowPtr[toSize(r) + 1]; ++k) {
                if (r != 4) {
                    t.push_back({r, A.col[toSize(k)], A.val[toSize(k)]});
                }
            }
        }
        A = fromTriplets(30, 20, t).value();
    }
    const CsrMatrix B = randomSparse(20, 25, 4, 103);
    auto C = multiply(A, B);
    ASSERT_TRUE(C.ok());
    ASSERT_TRUE(validate(C.value()).ok());
    EXPECT_EQ(C.value().rowPtr[5] - C.value().rowPtr[4], 0);
    expectClose(toDense(C.value()), denseMultiply(A, B));
}

TEST(SparseOps, MultiplyAllocatesExactly) {
    const CsrMatrix A = randomSparse(50, 50, 5, 104);
    auto C = multiply(A, A);
    ASSERT_TRUE(C.ok());
    EXPECT_EQ(C.value().col.size(), toSize(C.value().nnz()));
    EXPECT_EQ(C.value().val.size(), toSize(C.value().nnz()));
}

TEST(SparseOps, GalerkinProductOfSymmetricMatrixIsSymmetric) {
    // Kc = Pᵀ (K P), the AMG coarse operator, must be symmetric when K is.
    const CsrMatrix K = randomSparse(40, 40, 4, 105, /*symmetric=*/true);
    ASSERT_TRUE(isSymmetric(K));
    const CsrMatrix P = randomSparse(40, 9, 1, 106);
    auto KP = multiply(K, P);
    ASSERT_TRUE(KP.ok());
    auto Kc = multiply(transpose(P), KP.value());
    ASSERT_TRUE(Kc.ok());
    EXPECT_TRUE(isSymmetric(Kc.value(), 1e-12));
    expectClose(toDense(KP.value()), denseMultiply(K, P));
    expectClose(toDense(Kc.value()), denseMultiply(transpose(P), KP.value()));
}

TEST(SparseOps, MultiplyIsDeterministic) {
    const CsrMatrix A = randomSparse(80, 60, 6, 107);
    const CsrMatrix B = randomSparse(60, 70, 6, 108);
    auto C1 = multiply(A, B);
    auto C2 = multiply(A, B);
    ASSERT_TRUE(C1.ok() && C2.ok());
    EXPECT_EQ(C1.value().col, C2.value().col);
    EXPECT_EQ(C1.value().val, C2.value().val);
}

// V3c: the pool version is bit-identical to the serial product for any thread count, over
// several chunks (kChunkRows = 4096) and repeated calls that reuse the per-thread work arrays.
TEST(SparseOps, ParallelMultiplyIsBitIdenticalToSerial) {
    const CsrMatrix A = randomSparse(20000, 9000, 9, 111);
    const CsrMatrix B = randomSparse(9000, 7000, 7, 112);
    auto serial = multiply(A, B);
    ASSERT_TRUE(serial.ok());
    for (const int threads : {1, 3, 8}) {
        KernelPool pool(threads);
        for (int repeat = 0; repeat < 2; ++repeat) {
            auto parallel = multiply(pool, A, B);
            ASSERT_TRUE(parallel.ok());
            EXPECT_EQ(parallel.value().rowPtr, serial.value().rowPtr) << threads;
            EXPECT_EQ(parallel.value().col, serial.value().col) << threads;
            EXPECT_EQ(parallel.value().val, serial.value().val) << threads;
        }
        // A smaller product afterwards: stale work-array marks must not leak into it.
        const CsrMatrix S = randomSparse(50, 40, 4, 113);
        const CsrMatrix T = randomSparse(40, 30, 4, 114);
        EXPECT_EQ(multiply(pool, S, T).value().val, multiply(S, T).value().val);
    }
    KernelPool pool(2);
    EXPECT_EQ(multiply(pool, randomSparse(3, 4, 1, 115), randomSparse(5, 2, 1, 116)).error().code,
              ErrorCode::InvalidInput);
}

TEST(SparseOps, MultiplyRejectsMismatchedSizes) {
    const CsrMatrix A = randomSparse(3, 4, 1, 109);
    const CsrMatrix B = randomSparse(5, 2, 1, 110);
    EXPECT_EQ(multiply(A, B).error().code, ErrorCode::InvalidInput);
}
