#pragma once

#include "core/common/Result.hpp"
#include "core/sparse/CsrMatrix.hpp"

namespace uniforge::sparse {

// Matrix–matrix operations for the AMG setup (spec §5.4: Galerkin product Kc = Pᵀ(K·P)).
// They live in `sparse` so they are tested before AMG is built (spec §13, V2-1). Serial and
// deterministic: the same inputs give bit-identical outputs.

// Aᵀ, with sorted columns. Counting sort: O(nnz + rows + cols).
[[nodiscard]] CsrMatrix transpose(const CsrMatrix& A);

// C = A·B (Gustavson's row-by-row algorithm). A symbolic pass counts the entries of every row
// first, so C is allocated once at its exact size; the numeric pass accumulates each row in a
// dense work array in a fixed order (A's row order, then B's), then sorts the row's columns.
// Entries that cancel to exactly 0 are kept (the pattern is structural). InvalidInput when
// A.cols != B.rows.
[[nodiscard]] Result<CsrMatrix> multiply(const CsrMatrix& A, const CsrMatrix& B);

} // namespace uniforge::sparse
