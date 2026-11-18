#pragma once

#include <vector>

#include "core/sparse/CsrMatrix.hpp"
#include "core/sparse/KernelPool.hpp"

namespace uniforge::sparse {

using Vector = std::vector<double>;

// Shared kernels for every solver (spec §5.5, N1: all solvers use the same code). Each one is
// split into the pool's fixed chunks, so the result is bit-identical for any thread count (N2).
// Sizes are preconditions (asserted in debug builds): vectors of matching length, x of length
// A.cols and y of length A.rows for the matrix–vector products. Hot paths do not allocate.

// xᵀy
[[nodiscard]] double dot(KernelPool& pool, const Vector& x, const Vector& y);
// Σ xᵢ (used for 1ᵀr, the heat imbalance of a residual, spec §3.5)
[[nodiscard]] double sum(KernelPool& pool, const Vector& x);
// ‖x‖₂ = sqrt(xᵀx)
[[nodiscard]] double norm2(KernelPool& pool, const Vector& x);
// ‖x‖∞ = max |xᵢ|; NaN if any entry is NaN; 0 for an empty vector
[[nodiscard]] double normInf(KernelPool& pool, const Vector& x);
// ‖x − y‖∞ (error against a reference solution, spec §7)
[[nodiscard]] double maxAbsDiff(KernelPool& pool, const Vector& x, const Vector& y);

// y ← y + a·x
void axpy(KernelPool& pool, double a, const Vector& x, Vector& y);
// y ← x + b·y
void xpay(KernelPool& pool, const Vector& x, double b, Vector& y);
// z ← x ⊙ y (entry-wise product, e.g. Jacobi preconditioner application)
void multiplyPointwise(KernelPool& pool, const Vector& x, const Vector& y, Vector& z);

// y ← A·x
void spmv(KernelPool& pool, const CsrMatrix& A, const Vector& x, Vector& y);
// r ← b − A·x
void residual(KernelPool& pool, const CsrMatrix& A, const Vector& x, const Vector& b, Vector& r);

// Diagonal of a square matrix (0 where no diagonal entry is stored).
[[nodiscard]] Vector diagonal(const CsrMatrix& A);

} // namespace uniforge::sparse
