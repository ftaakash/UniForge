#include "core/solvers/krylov/Preconditioner.hpp"

#include <string>

namespace uniforge::solvers {

SetupReport IdentityPreconditioner::setup(const CsrMatrix& K, sparse::KernelPool& pool) {
    pool_ = &pool;
    n_ = static_cast<double>(K.rows);
    return {};
}

void IdentityPreconditioner::apply(const Vector& r, Vector& z) {
    const Index n = static_cast<Index>(r.size());
    pool_->parallelFor(n, [&](Index begin, Index end) {
        for (Index i = begin; i < end; ++i) {
            z[sparse::toSize(i)] = r[sparse::toSize(i)];
        }
    });
}

SetupReport JacobiPreconditioner::setup(const CsrMatrix& K, sparse::KernelPool& pool) {
    pool_ = &pool;
    SetupReport report;
    invDiag_ = sparse::diagonal(K);
    for (std::size_t i = 0; i < invDiag_.size(); ++i) {
        if (!(invDiag_[i] > 0.0)) {
            report.status = SolveStatus::Breakdown;
            report.detail = "Diagonal entry K[" + std::to_string(i) + "][" + std::to_string(i) +
                            "] = " + std::to_string(invDiag_[i]) +
                            " is not positive, so K is not symmetric positive definite.";
            return report;
        }
        invDiag_[i] = 1.0 / invDiag_[i];
    }
    report.footprint = footprint();
    return report;
}

void JacobiPreconditioner::apply(const Vector& r, Vector& z) {
    sparse::multiplyPointwise(*pool_, invDiag_, r, z);
}

MemoryFootprint JacobiPreconditioner::footprint() const {
    MemoryFootprint f;
    f.add("Jacobi inverse diagonal", invDiag_.capacity() * sizeof(double));
    return f;
}

} // namespace uniforge::solvers
