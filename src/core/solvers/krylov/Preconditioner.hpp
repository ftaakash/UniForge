#pragma once

#include <string_view>

#include "core/solvers/SolverTypes.hpp"
#include "core/sparse/KernelPool.hpp"

namespace uniforge::solvers {

// z = M⁻¹·r for preconditioned CG (build spec §5.3). M must be symmetric positive definite.
class Preconditioner {
public:
    virtual ~Preconditioner() = default;
    Preconditioner() = default;
    Preconditioner(const Preconditioner&) = delete;
    Preconditioner& operator=(const Preconditioner&) = delete;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    // Mesh information for preconditioners that use it (geometric multigrid, V3d), given by PCG
    // before setup(). The arrays must outlive setup(). Default: ignored.
    virtual void setHint(const CoordinateHint& /*hint*/) {}
    // Status Converged = ready; Breakdown when M cannot be built (detail says why).
    virtual SetupReport setup(const CsrMatrix& K, sparse::KernelPool& pool) = 0;
    virtual void apply(const Vector& r, Vector& z) = 0;
    [[nodiscard]] virtual MemoryFootprint footprint() const = 0;
    // Bytes read and written by one apply(), for the work accounting (amendment A9).
    [[nodiscard]] virtual double bytesPerApply() const noexcept = 0;
};

// M = I: plain conjugate gradients (tests and reference runs; not one of the six variants).
class IdentityPreconditioner final : public Preconditioner {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "none"; }
    SetupReport setup(const CsrMatrix& K, sparse::KernelPool& pool) override;
    void apply(const Vector& r, Vector& z) override;
    [[nodiscard]] MemoryFootprint footprint() const override { return {}; }
    [[nodiscard]] double bytesPerApply() const noexcept override { return 16.0 * n_; }

private:
    sparse::KernelPool* pool_ = nullptr;
    double n_ = 0.0;
};

// M = diag(K) (Jacobi): z_i = r_i / K_ii. Parallel, cannot break down on an SPD matrix; setup
// reports Breakdown if a diagonal entry is ≤ 0 (then K is not SPD).
class JacobiPreconditioner final : public Preconditioner {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "Jacobi"; }
    SetupReport setup(const CsrMatrix& K, sparse::KernelPool& pool) override;
    void apply(const Vector& r, Vector& z) override;
    [[nodiscard]] MemoryFootprint footprint() const override;
    [[nodiscard]] double bytesPerApply() const noexcept override {
        return 24.0 * static_cast<double>(invDiag_.size());
    }

private:
    sparse::KernelPool* pool_ = nullptr;
    Vector invDiag_;
};

} // namespace uniforge::solvers
