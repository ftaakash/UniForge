// CSR storage (spec §6.2): construction from triplets and patterns, invariants, lookup, symmetry.

#include "core/sparse/CsrMatrix.hpp"

#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

using namespace uniforge::sparse;
using uniforge::ErrorCode;

namespace {

// 3 × 3:  [ 4 −1  0 ]
//         [−1  4 −1 ]
//         [ 0 −1  4 ]
CsrMatrix tridiag3() {
    auto m = fromTriplets(
        3, 3, {{0, 0, 4}, {0, 1, -1}, {1, 0, -1}, {1, 1, 4}, {1, 2, -1}, {2, 1, -1}, {2, 2, 4}});
    EXPECT_TRUE(m.ok());
    return std::move(m).value();
}

} // namespace

TEST(CsrMatrix, FromTripletsSortsAndSumsDuplicates) {
    auto r = fromTriplets(2, 3, {{1, 2, 5.0}, {0, 1, 1.0}, {1, 0, 2.0}, {0, 1, 0.5}, {1, 2, -1.0}});
    ASSERT_TRUE(r.ok());
    const CsrMatrix& m = r.value();
    EXPECT_EQ(m.rowPtr, (std::vector<Offset>{0, 1, 3}));
    EXPECT_EQ(m.col, (std::vector<Index>{1, 0, 2}));
    EXPECT_EQ(m.val, (std::vector<double>{1.5, 2.0, 4.0}));
    EXPECT_EQ(m.nnz(), 3);
    EXPECT_TRUE(validate(m).ok());
}

TEST(CsrMatrix, FromTripletsHandlesEmptyRowsAndEmptyMatrix) {
    auto r = fromTriplets(4, 4, {{3, 3, 1.0}});
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().rowPtr, (std::vector<Offset>{0, 0, 0, 0, 1}));
    auto e = fromTriplets(0, 0, {});
    ASSERT_TRUE(e.ok());
    EXPECT_EQ(e.value().nnz(), 0);
    EXPECT_TRUE(validate(e.value()).ok());
}

TEST(CsrMatrix, FromTripletsRejectsBadInput) {
    EXPECT_EQ(fromTriplets(2, 2, {{2, 0, 1.0}}).error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(fromTriplets(2, 2, {{0, -1, 1.0}}).error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(fromTriplets(-1, 2, {}).error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(fromTriplets(2, 2, {{0, 0, std::numeric_limits<double>::quiet_NaN()}}).error().code,
              ErrorCode::NonFinite);
}

TEST(CsrMatrix, FindAndAt) {
    const CsrMatrix m = tridiag3();
    EXPECT_EQ(m.find(1, 2), 4);
    EXPECT_EQ(m.find(0, 2), -1);
    EXPECT_EQ(m.find(3, 0), -1);
    EXPECT_EQ(m.find(-1, 0), -1);
    EXPECT_DOUBLE_EQ(m.at(2, 2), 4.0);
    EXPECT_DOUBLE_EQ(m.at(2, 0), 0.0);
}

TEST(CsrMatrix, BuildPatternSortsDeduplicatesAndChecksRange) {
    auto p = buildPattern(3, 5, [](Index r, std::vector<Index>& out) {
        out.insert(out.end(), {Index(r + 2), r, Index(r + 2), r});
    });
    ASSERT_TRUE(p.ok());
    EXPECT_EQ(p.value().rowPtr, (std::vector<Offset>{0, 2, 4, 6}));
    EXPECT_EQ(p.value().col, (std::vector<Index>{0, 2, 1, 3, 2, 4}));
    EXPECT_TRUE(validate(p.value()).ok());

    auto bad = buildPattern(2, 2, [](Index r, std::vector<Index>& out) { out.push_back(r + 1); });
    EXPECT_EQ(bad.error().code, ErrorCode::InvalidInput);
}

TEST(CsrMatrix, MakeMatrixHasZeroValues) {
    auto p = buildPattern(2, 2, [](Index, std::vector<Index>& out) { out = {0, 1}; });
    ASSERT_TRUE(p.ok());
    const CsrMatrix m = makeMatrix(std::move(p).value());
    EXPECT_EQ(m.val, (std::vector<double>{0, 0, 0, 0}));
    EXPECT_TRUE(validate(m).ok());
    EXPECT_GE(m.bytes(), 3 * sizeof(Offset) + 4 * sizeof(Index) + 4 * sizeof(double));
}

TEST(CsrMatrix, ValidateCatchesEveryBrokenInvariant) {
    const CsrMatrix good = tridiag3();
    ASSERT_TRUE(validate(good).ok());

    CsrMatrix m = good;
    m.rowPtr[0] = 1;
    EXPECT_FALSE(validate(m).ok()) << "rowPtr[0] != 0";
    m = good;
    m.rowPtr.pop_back();
    EXPECT_FALSE(validate(m).ok()) << "rowPtr too short";
    m = good;
    std::swap(m.col[2], m.col[3]);
    EXPECT_FALSE(validate(m).ok()) << "unsorted row";
    m = good;
    m.col[3] = m.col[2];
    EXPECT_FALSE(validate(m).ok()) << "duplicate column";
    m = good;
    m.col[0] = 3;
    EXPECT_FALSE(validate(m).ok()) << "column out of range";
    m = good;
    m.val.pop_back();
    EXPECT_FALSE(validate(m).ok()) << "missing value";
    m = good;
    m.val[1] = std::numeric_limits<double>::infinity();
    EXPECT_EQ(validate(m).error().code, ErrorCode::NonFinite);
    m = good;
    m.rowPtr[2] = 1; // rowPtr decreases: 0, 2, 1, 7
    EXPECT_FALSE(validate(m).ok()) << "decreasing rowPtr";
}

TEST(CsrMatrix, SymmetryCheck) {
    CsrMatrix m = tridiag3();
    EXPECT_TRUE(isSymmetric(m));
    m.val[1] = -1.0 + 1e-14; // a_01 differs from a_10 in the last bits
    EXPECT_FALSE(isSymmetric(m));
    EXPECT_TRUE(isSymmetric(m, 1e-12));

    // An entry stored on one side only counts as asymmetric.
    auto one = fromTriplets(2, 2, {{0, 0, 1}, {0, 1, 2}, {1, 1, 1}});
    EXPECT_FALSE(isSymmetric(one.value()));
    auto rect = fromTriplets(2, 3, {});
    EXPECT_FALSE(isSymmetric(rect.value()));
}

TEST(CsrMatrix, ToDense) {
    const auto d = toDense(tridiag3());
    EXPECT_EQ(d, (std::vector<double>{4, -1, 0, -1, 4, -1, 0, -1, 4}));
}
