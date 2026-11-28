#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "core/common/Result.hpp"

namespace uniforge::sparse {

// Row and column numbers. 32 bits are enough for every V2 problem (≤ ~3.2e6 rows, spec §1.3).
using Index = std::int32_t;
// Positions in the column/value arrays. 64 bits, so a matrix may hold more than 2^31 entries
// (the Cholesky factor can approach 1e9, spec §5.2).
using Offset = std::int64_t;

[[nodiscard]] constexpr std::size_t toSize(Index i) noexcept {
    return static_cast<std::size_t>(i);
}
[[nodiscard]] constexpr std::size_t toSize(Offset i) noexcept {
    return static_cast<std::size_t>(i);
}

// Compressed sparse row structure without values (spec §6.2). Row r owns the entries
// rowPtr[r] … rowPtr[r+1]−1 of `col`, with column numbers strictly increasing inside a row.
struct CsrPattern {
    Index rows = 0;
    Index cols = 0;
    std::vector<Offset> rowPtr{0}; // rows + 1 entries, rowPtr[0] == 0
    std::vector<Index> col;

    [[nodiscard]] Offset nnz() const noexcept { return rowPtr.empty() ? 0 : rowPtr.back(); }
};

// CSR matrix: a pattern plus one double per stored entry. Symmetric matrices are stored in full
// (both triangles), which keeps the matrix–vector product a single pass (spec §6.2).
struct CsrMatrix {
    Index rows = 0;
    Index cols = 0;
    std::vector<Offset> rowPtr{0};
    std::vector<Index> col;
    std::vector<double> val;

    [[nodiscard]] Offset nnz() const noexcept { return rowPtr.empty() ? 0 : rowPtr.back(); }

    // Position of entry (r, c) in col/val, or −1 when it is not stored. Binary search in row r.
    [[nodiscard]] Offset find(Index r, Index c) const noexcept;
    // Stored value of (r, c), 0 when the entry is not stored.
    [[nodiscard]] double at(Index r, Index c) const noexcept;
    // Bytes held by the three arrays (capacity × element size), for memory accounting.
    [[nodiscard]] std::size_t bytes() const noexcept;
};

// A matrix with the given pattern and all values 0 (the assembly target, spec §3.5).
[[nodiscard]] CsrMatrix makeMatrix(CsrPattern pattern);

// One (row, column, value) entry, for building small matrices in tests and tools. The FE
// assembly does not use triplets (spec §3.5, amendment A4).
struct Triplet {
    Index row;
    Index col;
    double value;
};

// Sorts the triplets by (row, column) and sums duplicates. InvalidInput when a row or column is
// out of range or a size is negative; NonFinite when a value is NaN or infinite.
[[nodiscard]] Result<CsrMatrix> fromTriplets(Index rows, Index cols, std::vector<Triplet> triplets);

// Builds a pattern row by row: `rowColumns(r, out)` appends the column numbers of row r to `out`
// (any order, duplicates allowed); the builder sorts and removes duplicates. InvalidInput when a
// column is out of range or a size is negative.
using RowColumnsFn = std::function<void(Index row, std::vector<Index>& out)>;
[[nodiscard]] Result<CsrPattern> buildPattern(Index rows, Index cols,
                                              const RowColumnsFn& rowColumns);

// Checks every CSR invariant: sizes, rowPtr starting at 0 and non-decreasing, columns in range
// and strictly increasing per row, one value per entry (matrix only), finite values.
[[nodiscard]] Result<void> validate(const CsrPattern& pattern);
[[nodiscard]] Result<void> validate(const CsrMatrix& matrix);

// True when the matrix is square and |a_ij − a_ji| ≤ tol·max(|a_ij|, |a_ji|) for every stored entry
// (an entry missing on one side counts as 0). tol = 0 asks for exact symmetry.
[[nodiscard]] bool isSymmetric(const CsrMatrix& matrix, double tol = 0.0);

// Dense copy, row-major (rows × cols). For tests and small debugging cases only.
[[nodiscard]] std::vector<double> toDense(const CsrMatrix& matrix);

} // namespace uniforge::sparse
