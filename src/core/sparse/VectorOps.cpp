#include "core/sparse/VectorOps.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace uniforge::sparse {

namespace {

Index length(const Vector& x) {
    return static_cast<Index>(x.size());
}

double rowProduct(const CsrMatrix& A, const Vector& x, Index r) {
    double s = 0.0;
    for (Offset k = A.rowPtr[toSize(r)]; k < A.rowPtr[toSize(r) + 1]; ++k) {
        s += A.val[toSize(k)] * x[toSize(A.col[toSize(k)])];
    }
    return s;
}

} // namespace

double dot(KernelPool& pool, const Vector& x, const Vector& y) {
    assert(x.size() == y.size());
    return pool.reduceSum(length(x), [&](Index begin, Index end) {
        double s = 0.0;
        for (Index i = begin; i < end; ++i) {
            s += x[toSize(i)] * y[toSize(i)];
        }
        return s;
    });
}

double sum(KernelPool& pool, const Vector& x) {
    return pool.reduceSum(length(x), [&](Index begin, Index end) {
        double s = 0.0;
        for (Index i = begin; i < end; ++i) {
            s += x[toSize(i)];
        }
        return s;
    });
}

double norm2(KernelPool& pool, const Vector& x) {
    return std::sqrt(dot(pool, x, x));
}

double normInf(KernelPool& pool, const Vector& x) {
    if (x.empty()) {
        return 0.0;
    }
    return pool.reduceMax(length(x), [&](Index begin, Index end) {
        double m = 0.0;
        for (Index i = begin; i < end; ++i) {
            const double a = std::fabs(x[toSize(i)]);
            if (std::isnan(a)) {
                return a;
            }
            m = std::max(m, a);
        }
        return m;
    });
}

double maxAbsDiff(KernelPool& pool, const Vector& x, const Vector& y) {
    assert(x.size() == y.size());
    if (x.empty()) {
        return 0.0;
    }
    return pool.reduceMax(length(x), [&](Index begin, Index end) {
        double m = 0.0;
        for (Index i = begin; i < end; ++i) {
            const double a = std::fabs(x[toSize(i)] - y[toSize(i)]);
            if (std::isnan(a)) {
                return a;
            }
            m = std::max(m, a);
        }
        return m;
    });
}

void axpy(KernelPool& pool, double a, const Vector& x, Vector& y) {
    assert(x.size() == y.size());
    pool.parallelFor(length(x), [&](Index begin, Index end) {
        for (Index i = begin; i < end; ++i) {
            y[toSize(i)] += a * x[toSize(i)];
        }
    });
}

void xpay(KernelPool& pool, const Vector& x, double b, Vector& y) {
    assert(x.size() == y.size());
    pool.parallelFor(length(x), [&](Index begin, Index end) {
        for (Index i = begin; i < end; ++i) {
            y[toSize(i)] = x[toSize(i)] + b * y[toSize(i)];
        }
    });
}

void multiplyPointwise(KernelPool& pool, const Vector& x, const Vector& y, Vector& z) {
    assert(x.size() == y.size() && x.size() == z.size());
    pool.parallelFor(length(x), [&](Index begin, Index end) {
        for (Index i = begin; i < end; ++i) {
            z[toSize(i)] = x[toSize(i)] * y[toSize(i)];
        }
    });
}

void spmv(KernelPool& pool, const CsrMatrix& A, const Vector& x, Vector& y) {
    assert(x.size() == toSize(A.cols) && y.size() == toSize(A.rows));
    assert(&x != &y);
    pool.parallelFor(A.rows, [&](Index begin, Index end) {
        for (Index r = begin; r < end; ++r) {
            y[toSize(r)] = rowProduct(A, x, r);
        }
    });
}

void residual(KernelPool& pool, const CsrMatrix& A, const Vector& x, const Vector& b, Vector& r) {
    assert(x.size() == toSize(A.cols) && b.size() == toSize(A.rows) && r.size() == b.size());
    assert(&x != &r);
    pool.parallelFor(A.rows, [&](Index begin, Index end) {
        for (Index i = begin; i < end; ++i) {
            r[toSize(i)] = b[toSize(i)] - rowProduct(A, x, i);
        }
    });
}

Vector diagonal(const CsrMatrix& A) {
    const Index n = std::min(A.rows, A.cols);
    Vector d(toSize(n), 0.0);
    for (Index r = 0; r < n; ++r) {
        d[toSize(r)] = A.at(r, r);
    }
    return d;
}

} // namespace uniforge::sparse
