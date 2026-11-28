#include "core/solvers/krylov/Ic0.hpp"

#include <cmath>
#include <sstream>
#include <string>
#include <utility>

namespace uniforge::solvers {

namespace {

using sparse::Offset;
using sparse::toSize;

std::string number(double v) {
    std::ostringstream s;
    s.precision(3);
    s << v;
    return s.str();
}

} // namespace

Ic0Preconditioner::Ic0Preconditioner(std::vector<double> shifts)
    : shifts_(std::move(shifts)) {
    if (shifts_.empty()) {
        shifts_.push_back(0.0);
    }
}

Index Ic0Preconditioner::factorise(const CsrMatrix& K, double alpha) {
    const Index n = K.rows;
    // Lower-triangle pattern of K; columns are sorted, so the diagonal is last in each row.
    l_ = CsrMatrix{};
    l_.rows = n;
    l_.cols = n;
    l_.rowPtr.assign(toSize(n) + 1, 0);
    for (Index i = 0; i < n; ++i) {
        Offset count = 0;
        for (Offset q = K.rowPtr[toSize(i)]; q < K.rowPtr[toSize(i) + 1]; ++q) {
            if (K.col[toSize(q)] <= i) {
                ++count;
            }
        }
        l_.rowPtr[toSize(i) + 1] = l_.rowPtr[toSize(i)] + count;
    }
    l_.col.resize(toSize(l_.rowPtr.back()));
    l_.val.resize(toSize(l_.rowPtr.back()));
    diag_.assign(toSize(n), -1);
    for (Index i = 0; i < n; ++i) {
        Offset at = l_.rowPtr[toSize(i)];
        for (Offset q = K.rowPtr[toSize(i)]; q < K.rowPtr[toSize(i) + 1]; ++q) {
            const Index j = K.col[toSize(q)];
            if (j <= i) {
                l_.col[toSize(at)] = j;
                l_.val[toSize(at)] = j == i ? K.val[toSize(q)] * (1.0 + alpha) : K.val[toSize(q)];
                if (j == i) {
                    diag_[toSize(i)] = at;
                }
                ++at;
            }
        }
        if (diag_[toSize(i)] < 0) {
            return i; // no diagonal entry: cannot factorise
        }
    }

    for (Index i = 0; i < n; ++i) {
        const Offset rowBegin = l_.rowPtr[toSize(i)];
        const Offset di = diag_[toSize(i)];
        for (Offset p = rowBegin; p < di; ++p) {
            const Index j = l_.col[toSize(p)];
            // Σ_{k<j} L_ik·L_jk over the common pattern of rows i and j (both sorted).
            double s = l_.val[toSize(p)];
            Offset a = rowBegin;
            Offset b = l_.rowPtr[toSize(j)];
            const Offset bEnd = diag_[toSize(j)];
            while (a < p && b < bEnd) {
                const Index ca = l_.col[toSize(a)];
                const Index cb = l_.col[toSize(b)];
                if (ca == cb) {
                    s -= l_.val[toSize(a)] * l_.val[toSize(b)];
                    ++a;
                    ++b;
                } else if (ca < cb) {
                    ++a;
                } else {
                    ++b;
                }
            }
            l_.val[toSize(p)] = s / l_.val[toSize(diag_[toSize(j)])];
        }
        double d = l_.val[toSize(di)];
        for (Offset p = rowBegin; p < di; ++p) {
            d -= l_.val[toSize(p)] * l_.val[toSize(p)];
        }
        if (!(d > 0.0)) {
            return i;
        }
        l_.val[toSize(di)] = std::sqrt(d);
    }
    return -1;
}

SetupReport Ic0Preconditioner::setup(const CsrMatrix& K, sparse::KernelPool& /*pool*/) {
    SetupReport report;
    for (std::size_t attempt = 0; attempt < shifts_.size(); ++attempt) {
        const double alpha = shifts_[attempt];
        const Index failedRow = factorise(K, alpha);
        if (failedRow < 0) {
            report.preconditionerShift = alpha;
            report.preconditionerRetries = static_cast<int>(attempt);
            if (attempt > 0) {
                report.detail = "IC(0) needed the shift α = " + number(alpha) + " after " +
                                std::to_string(attempt) + " failed attempt(s).";
            }
            report.footprint = footprint();
            return report;
        }
        report.detail = "IC(0) pivot ≤ 0 at row " + std::to_string(failedRow) +
                        " with shift α = " + number(alpha) + ".";
    }
    report.status = SolveStatus::Breakdown;
    report.preconditionerRetries = static_cast<int>(shifts_.size());
    report.detail += " Every shift of the ladder failed.";
    l_ = CsrMatrix{};
    diag_.clear();
    return report;
}

void Ic0Preconditioner::apply(const Vector& r, Vector& z) {
    const Index n = l_.rows;
    // Forward: L·y = r (y stored in z).
    for (Index i = 0; i < n; ++i) {
        double s = r[toSize(i)];
        const Offset di = diag_[toSize(i)];
        for (Offset p = l_.rowPtr[toSize(i)]; p < di; ++p) {
            s -= l_.val[toSize(p)] * z[toSize(l_.col[toSize(p)])];
        }
        z[toSize(i)] = s / l_.val[toSize(di)];
    }
    // Backward: Lᵀ·z = y, row i of L is column i of Lᵀ.
    for (Index i = n; i-- > 0;) {
        const Offset di = diag_[toSize(i)];
        const double zi = z[toSize(i)] / l_.val[toSize(di)];
        z[toSize(i)] = zi;
        for (Offset p = l_.rowPtr[toSize(i)]; p < di; ++p) {
            z[toSize(l_.col[toSize(p)])] -= l_.val[toSize(p)] * zi;
        }
    }
}

MemoryFootprint Ic0Preconditioner::footprint() const {
    MemoryFootprint f;
    f.add("IC(0) factor", l_.bytes() + diag_.capacity() * sizeof(Offset));
    return f;
}

double Ic0Preconditioner::bytesPerApply() const noexcept {
    return 2.0 * 12.0 * static_cast<double>(l_.nnz()) +
           2.0 * 8.0 * static_cast<double>(l_.rows + 1) + 32.0 * static_cast<double>(l_.rows);
}

} // namespace uniforge::solvers
