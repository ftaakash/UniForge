#pragma once

#include <vector>

#include "core/solvers/krylov/Preconditioner.hpp"

namespace uniforge::solvers {

// Incomplete Cholesky with zero fill, IC(0) (build spec §5.3): L has exactly the pattern of the
// lower triangle of K, computed row by row,
//
//     L_ij = (K_ij − Σ_{k<j} L_ik·L_jk) / L_jj          for j < i, (i, j) in the pattern
//     L_ii = sqrt(K_ii − Σ_{k<i} L_ik²)
//
// with the sums restricted to the pattern (entries outside it are dropped). M = L·Lᵀ.
//
// IC(0) can break down on an SPD matrix that is not an M-matrix: a pivot under the square root
// becomes ≤ 0 because dropped fill was needed [VERIFY: Kershaw 1978 example; Manteuffel 1980 for
// the shifted variant]. Then K + α·diag(K) is factorised instead, with α taken from the shift
// ladder in order (amendment A7); the α used and the number of failed attempts are reported.
// If every shift fails, setup reports Breakdown.
//
// apply() is two sparse triangular solves (forward with L, backward with Lᵀ), serial.
class Ic0Preconditioner final : public Preconditioner {
public:
    explicit Ic0Preconditioner(std::vector<double> shifts = {0.0, 1e-3, 1e-2, 1e-1, 1.0});

    [[nodiscard]] std::string_view name() const noexcept override { return "IC(0)"; }
    SetupReport setup(const CsrMatrix& K, sparse::KernelPool& pool) override;
    void apply(const Vector& r, Vector& z) override;
    [[nodiscard]] MemoryFootprint footprint() const override;
    [[nodiscard]] double bytesPerApply() const noexcept override;

    // The incomplete factor in CSR (row i: columns j ≤ i, diagonal last), for tests.
    [[nodiscard]] const CsrMatrix& factor() const noexcept { return l_; }

private:
    // One attempt with shift α. Returns the row of the first non-positive pivot, or −1.
    Index factorise(const CsrMatrix& K, double alpha);

    std::vector<double> shifts_;
    CsrMatrix l_;
    std::vector<sparse::Offset> diag_; // position of L_ii in l_
};

} // namespace uniforge::solvers
