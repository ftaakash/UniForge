#pragma once

// Test helpers: reproducible random sparse matrices and vectors (fixed seeds, v1 Rng).

#include <cstdint>
#include <utility>
#include <vector>

#include "core/common/Rng.hpp"
#include "core/sparse/CsrMatrix.hpp"

namespace uniforge::testing {

// rows × cols with about `perRow` random entries per row, values in [−1, 1); if `symmetric`
// (rows must equal cols), the matrix is A + Aᵀ plus a diagonal that makes it strictly
// diagonally dominant, i.e. symmetric positive definite.
inline sparse::CsrMatrix randomSparse(sparse::Index rows, sparse::Index cols, int perRow,
                                      std::uint64_t seed, bool symmetric = false) {
    Rng rng(seed);
    std::vector<sparse::Triplet> t;
    for (sparse::Index r = 0; r < rows; ++r) {
        for (int k = 0; k < perRow; ++k) {
            const auto c =
                static_cast<sparse::Index>(rng.next() % static_cast<std::uint64_t>(cols));
            const double v = rng.uniform(-1.0, 1.0);
            t.push_back({r, c, v});
            if (symmetric) {
                t.push_back({c, r, v});
            }
        }
    }
    if (symmetric) {
        for (sparse::Index r = 0; r < rows; ++r) {
            t.push_back({r, r, 4.0 * perRow + 1.0});
        }
    }
    return sparse::fromTriplets(rows, cols, std::move(t)).value();
}

// Values spanning many magnitudes and both signs, so that the order of a floating-point sum
// changes its last bits (a determinism test that passes by luck on benign data proves nothing).
inline std::vector<double> randomVector(sparse::Index n, std::uint64_t seed) {
    Rng rng(seed);
    std::vector<double> x(static_cast<std::size_t>(n));
    for (double& v : x) {
        const double scale = (rng.next() % 3 == 0) ? 1e8 : ((rng.next() % 2 == 0) ? 1.0 : 1e-8);
        v = rng.uniform(-1.0, 1.0) * scale;
    }
    return x;
}

} // namespace uniforge::testing
