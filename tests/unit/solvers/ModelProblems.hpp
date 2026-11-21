#pragma once

// Test helpers: model problems with known properties for the solver tests.

#include <algorithm>
#include <cmath>
#include <cstdint>
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

// 3D 7-point Laplacian on an m × m × m interior grid (Dirichlet boundary eliminated).
inline sparse::CsrMatrix laplacian3d(sparse::Index m) {
    std::vector<sparse::Triplet> t;
    const auto id = [m](sparse::Index i, sparse::Index j, sparse::Index k) {
        return i + m * (j + m * k);
    };
    for (sparse::Index k = 0; k < m; ++k) {
        for (sparse::Index j = 0; j < m; ++j) {
            for (sparse::Index i = 0; i < m; ++i) {
                const sparse::Index r = id(i, j, k);
                t.push_back({r, r, 6.0});
                if (i > 0)
                    t.push_back({r, id(i - 1, j, k), -1.0});
                if (i + 1 < m)
                    t.push_back({r, id(i + 1, j, k), -1.0});
                if (j > 0)
                    t.push_back({r, id(i, j - 1, k), -1.0});
                if (j + 1 < m)
                    t.push_back({r, id(i, j + 1, k), -1.0});
                if (k > 0)
                    t.push_back({r, id(i, j, k - 1), -1.0});
                if (k + 1 < m)
                    t.push_back({r, id(i, j, k + 1), -1.0});
            }
        }
    }
    return sparse::fromTriplets(m * m * m, m * m * m, std::move(t)).value();
}

// Symmetric permutation P·A·Pᵀ with a fixed pseudo-random P (to hide the natural grid order).
inline sparse::CsrMatrix shuffled(const sparse::CsrMatrix& A, std::uint64_t seed) {
    std::vector<sparse::Index> p(static_cast<std::size_t>(A.rows));
    for (sparse::Index i = 0; i < A.rows; ++i) {
        p[static_cast<std::size_t>(i)] = i;
    }
    std::uint64_t state = seed;
    for (std::size_t i = p.size(); i > 1; --i) { // Fisher–Yates with a fixed LCG
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        std::swap(p[i - 1], p[static_cast<std::size_t>((state >> 33) % i)]);
    }
    std::vector<sparse::Triplet> t;
    for (sparse::Index r = 0; r < A.rows; ++r) {
        for (sparse::Offset k = A.rowPtr[sparse::toSize(r)]; k < A.rowPtr[sparse::toSize(r) + 1];
             ++k) {
            t.push_back({p[sparse::toSize(r)], p[sparse::toSize(A.col[sparse::toSize(k)])],
                         A.val[sparse::toSize(k)]});
        }
    }
    return sparse::fromTriplets(A.rows, A.cols, std::move(t)).value();
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
