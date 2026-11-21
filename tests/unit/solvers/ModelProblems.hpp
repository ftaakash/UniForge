#pragma once

// Test helpers: model problems with known properties for the solver tests.

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "core/sparse/CsrMatrix.hpp"

namespace uniforge::testing {

// 1D Laplacian tridiag(−1, 2, −1) of size n (a rod held at both ends, after elimination).
// Its eigenvalues are 2 − 2·cos(kπ/(n+1)), k = 1 … n.
inline sparse::CsrMatrix laplacian1d(sparse::Index n) {
    std::vector<sparse::Triplet> t;
    for (sparse::Index i = 0; i < n; ++i) {
        t.push_back({i, i, 2.0});
        if (i > 0) {
            t.push_back({i, i - 1, -1.0});
        }
        if (i + 1 < n) {
            t.push_back({i, i + 1, -1.0});
        }
    }
    return sparse::fromTriplets(n, n, std::move(t)).value();
}

inline double laplacian1dCondition(sparse::Index n) {
    const double pi = std::acos(-1.0);
    const double lmin = 2.0 - 2.0 * std::cos(pi / (n + 1));
    const double lmax = 2.0 - 2.0 * std::cos(n * pi / (n + 1));
    return lmax / lmin;
}

// 2D 5-point Laplacian on an m × m interior grid (Dirichlet boundary eliminated).
inline sparse::CsrMatrix laplacian2d(sparse::Index m) {
    std::vector<sparse::Triplet> t;
    const auto id = [m](sparse::Index i, sparse::Index j) { return i + m * j; };
    for (sparse::Index j = 0; j < m; ++j) {
        for (sparse::Index i = 0; i < m; ++i) {
            t.push_back({id(i, j), id(i, j), 4.0});
            if (i > 0) {
                t.push_back({id(i, j), id(i - 1, j), -1.0});
            }
            if (i + 1 < m) {
                t.push_back({id(i, j), id(i + 1, j), -1.0});
            }
            if (j > 0) {
                t.push_back({id(i, j), id(i, j - 1), -1.0});
            }
            if (j + 1 < m) {
                t.push_back({id(i, j), id(i, j + 1), -1.0});
            }
        }
    }
    return sparse::fromTriplets(m * m, m * m, std::move(t)).value();
}

// D·A·D with D = diag(10^(−3 … 3)) spread over the rows: same SPD structure, badly scaled,
// the case where Jacobi preconditioning helps most.
inline sparse::CsrMatrix badlyScaled(const sparse::CsrMatrix& A) {
    sparse::CsrMatrix B = A;
    const auto scale = [&](sparse::Index i) {
        return std::pow(10.0, -3.0 + 6.0 * i / std::max<sparse::Index>(1, A.rows - 1));
    };
    for (sparse::Index r = 0; r < B.rows; ++r) {
        for (sparse::Offset k = B.rowPtr[sparse::toSize(r)]; k < B.rowPtr[sparse::toSize(r) + 1];
             ++k) {
            B.val[sparse::toSize(k)] *= scale(r) * scale(B.col[sparse::toSize(k)]);
        }
    }
    return B;
}

} // namespace uniforge::testing
