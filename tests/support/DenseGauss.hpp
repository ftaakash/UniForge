#pragma once

// Test-only oracle (spec D6): dense Gaussian elimination with partial pivoting. O(n³), used to
// check the Thomas solver on small systems; never linked into the product.

#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "core/thermal/Assembler.hpp"

namespace uniforge::testing {

using DenseMatrix = std::vector<std::vector<double>>;

inline DenseMatrix toDense(const thermal::TridiagonalSystem& s) {
    const std::size_t n = s.size();
    DenseMatrix m(n, std::vector<double>(n, 0.0));
    for (std::size_t i = 0; i < n; ++i) {
        if (i > 0) {
            m[i][i - 1] = s.a[i];
        }
        m[i][i] = s.b[i];
        if (i + 1 < n) {
            m[i][i + 1] = s.c[i];
        }
    }
    return m;
}

// Solves m·x = rhs. Returns an empty vector when the matrix is singular.
inline std::vector<double> denseSolve(DenseMatrix m, std::vector<double> rhs) {
    const std::size_t n = rhs.size();
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        for (std::size_t r = col + 1; r < n; ++r) {
            if (std::abs(m[r][col]) > std::abs(m[pivot][col])) {
                pivot = r;
            }
        }
        if (m[pivot][col] == 0.0) {
            return {};
        }
        std::swap(m[pivot], m[col]);
        std::swap(rhs[pivot], rhs[col]);
        for (std::size_t r = col + 1; r < n; ++r) {
            const double factor = m[r][col] / m[col][col];
            for (std::size_t c = col; c < n; ++c) {
                m[r][c] -= factor * m[col][c];
            }
            rhs[r] -= factor * rhs[col];
        }
    }
    std::vector<double> x(n, 0.0);
    for (std::size_t i = n; i-- > 0;) {
        double sum = rhs[i];
        for (std::size_t c = i + 1; c < n; ++c) {
            sum -= m[i][c] * x[c];
        }
        x[i] = sum / m[i][i];
    }
    return x;
}

} // namespace uniforge::testing
