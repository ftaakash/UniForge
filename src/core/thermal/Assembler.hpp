#pragma once

#include <cstddef>
#include <vector>

#include "core/thermal/RodProblem.hpp"

namespace uniforge::thermal {

// Tridiagonal system of n equations, row i:
//     a[i]·T[i−1] + b[i]·T[i] + c[i]·T[i+1] = f[i]
// a is the sub-diagonal (a[0] unused, 0), b the diagonal, c the super-diagonal (c[n−1] unused, 0).
struct TridiagonalSystem {
    std::vector<double> a, b, c, f;

    [[nodiscard]] std::size_t size() const noexcept { return b.size(); }
    // Resizes to n rows and zeroes every entry, reusing the existing capacity.
    void reset(std::size_t n);
};

// Assembles the global stiffness K (tridiagonal, symmetric: a[i+1] == c[i]) and load vector f
// of the rod problem by adding each element's Ke and fe into rows/columns (e, e+1). No boundary
// conditions are applied yet, so every row of K sums to 0 (a constant temperature produces no
// heat flow). Pre-condition: problem.validate() succeeded.
void assemble(const RodProblem& problem, TridiagonalSystem& system);

} // namespace uniforge::thermal
