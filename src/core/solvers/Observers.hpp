#pragma once

#include <vector>

#include "core/solvers/SolverTypes.hpp"
#include "core/sparse/KernelPool.hpp"

namespace uniforge::solvers {

// Called by iterative solvers after every iteration (build spec §7). The solver measures the
// time spent inside onIteration() and subtracts it from its reported solve time, so tracking
// the error against a reference does not distort the timing.
class IterationObserver {
public:
    virtual ~IterationObserver() = default;
    IterationObserver() = default;
    IterationObserver(const IterationObserver&) = delete;
    IterationObserver& operator=(const IterationObserver&) = delete;

    // k = 1, 2, …; relResidual = ‖r_k‖₂ / ‖f‖₂; x = the current iterate.
    virtual void onIteration(int k, double relResidual, const Vector& x) = 0;
};

// Records ‖x_k − T_ref‖∞ after every iteration (amendment A2: time-to-accuracy). The pool is
// used for the norm, so the cost is one O(n) pass.
class ErrorVsReference final : public IterationObserver {
public:
    ErrorVsReference(const Vector& reference, sparse::KernelPool& pool)
        : reference_(reference)
        , pool_(pool) {}

    void onIteration(int k, double relResidual, const Vector& x) override;

    [[nodiscard]] const std::vector<double>& errors() const noexcept { return errors_; }
    // First iteration whose error is ≤ target, or −1.
    [[nodiscard]] int firstIterationBelow(double target) const noexcept;

private:
    const Vector& reference_;
    sparse::KernelPool& pool_;
    std::vector<double> errors_;
};

// Keeps a copy of x every `every` iterations, for convergence pictures (build spec §7).
class Snapshot final : public IterationObserver {
public:
    explicit Snapshot(int every)
        : every_(every < 1 ? 1 : every) {}

    void onIteration(int k, double relResidual, const Vector& x) override;

    struct Shot {
        int iteration;
        Vector x;
    };
    [[nodiscard]] const std::vector<Shot>& shots() const noexcept { return shots_; }

private:
    int every_;
    std::vector<Shot> shots_;
};

} // namespace uniforge::solvers
