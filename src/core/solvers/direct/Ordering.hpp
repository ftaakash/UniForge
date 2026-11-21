#pragma once

#include <vector>

#include "core/sparse/CsrMatrix.hpp"

namespace uniforge::solvers {

using sparse::CsrMatrix;
using sparse::Index;

// Fill-reducing orderings for the sparse Cholesky factorisation (build spec §5.2). They use only
// the graph of the matrix (its off-diagonal pattern, assumed symmetric), so they work for any
// mesh and any design.

enum class Ordering { Natural, Rcm, NestedDissection };

// perm[k] = old index of new row/column k; inverse[old] = new index.
struct Permutation {
    std::vector<Index> perm;
    std::vector<Index> inverse;
};

// Identity.
[[nodiscard]] Permutation naturalOrdering(Index n);

// Reverse Cuthill–McKee [VERIFY: Cuthill & McKee 1969; reversal by George]: per connected
// component, start at a pseudo-peripheral node (repeated breadth-first search from the
// last-level node of smallest degree until the depth stops growing), number the nodes level by
// level with neighbours in increasing degree, then reverse. Keeps the nonzeros near the
// diagonal (small envelope), so fill stays inside it.
[[nodiscard]] Permutation rcmOrdering(const CsrMatrix& A);

// Nested dissection on the graph [VERIFY: George 1973]: split each connected subgraph by the
// middle level of a breadth-first level structure from a pseudo-peripheral node (a vertex
// separator: the levels before and after it are not adjacent), order the two parts
// recursively, and number the separator last. Subgraphs of at most `leafSize` nodes are
// numbered by RCM. Separators are numbered after everything they separate, so the elimination
// of one part creates no fill in the other.
[[nodiscard]] Permutation nestedDissectionOrdering(const CsrMatrix& A, int leafSize = 64);

[[nodiscard]] Permutation makeOrdering(Ordering ordering, const CsrMatrix& A, int leafSize = 64);

// True when perm is a permutation of 0 … n−1 and inverse is its inverse.
[[nodiscard]] bool isValid(const Permutation& p, Index n);

} // namespace uniforge::solvers
