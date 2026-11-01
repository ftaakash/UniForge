#pragma once

#include <vector>

#include "core/common/Result.hpp"
#include "core/thermal/Assembler.hpp"

namespace uniforge::thermal {

// Solves a tridiagonal system with the Thomas algorithm (Gaussian elimination specialised to
// three diagonals, O(n), no pivoting). `system` is not modified; `scratch` holds the modified
// super-diagonal and is resized as needed, so callers can reuse it across solves. `x` receives
// the solution (size n).
//
// No pivoting is safe for the rod matrix: after Dirichlet elimination it is symmetric positive
// definite and irreducibly diagonally dominant (|b_i| >= |a_i| + |c_i| in every row, strictly in
// the rows next to the boundary), so every pivot m_i = b_i − a_i·c'_{i−1} is positive.
// For any other input a zero or non-finite pivot is reported as Internal instead of producing
// inf/NaN.
Result<void> solveThomas(const TridiagonalSystem& system, std::vector<double>& scratch,
                         std::vector<double>& x);

} // namespace uniforge::thermal
