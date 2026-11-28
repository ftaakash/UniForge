#include "core/sparse/SparseOps.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "core/sparse/KernelPool.hpp"

namespace uniforge::sparse {

CsrMatrix transpose(const CsrMatrix& A) {
    CsrMatrix t;
    t.rows = A.cols;
    t.cols = A.rows;
    t.rowPtr.assign(toSize(A.cols) + 1, 0);
    for (const Index c : A.col) {
        ++t.rowPtr[toSize(c) + 1];
    }
    for (std::size_t c = 0; c < toSize(A.cols); ++c) {
        t.rowPtr[c + 1] += t.rowPtr[c];
    }
    t.col.resize(A.col.size());
    t.val.resize(A.val.size());
    std::vector<Offset> next(t.rowPtr.begin(), t.rowPtr.end() - 1);
    for (Index r = 0; r < A.rows; ++r) { // rows in increasing order → sorted columns in Aᵀ
        for (Offset k = A.rowPtr[toSize(r)]; k < A.rowPtr[toSize(r) + 1]; ++k) {
            const std::size_t dst = toSize(next[toSize(A.col[toSize(k)])]++);
            t.col[dst] = r;
            t.val[dst] = A.val[toSize(k)];
        }
    }
    return t;
}

Result<CsrMatrix> multiply(const CsrMatrix& A, const CsrMatrix& B) {
    if (A.cols != B.rows) {
        return makeError(ErrorCode::InvalidInput,
                         "Cannot multiply a " + std::to_string(A.rows) + " × " +
                             std::to_string(A.cols) + " matrix by a " + std::to_string(B.rows) +
                             " × " + std::to_string(B.cols) + " matrix.",
                         "This is a programming error in the caller.");
    }
    CsrMatrix C;
    C.rows = A.rows;
    C.cols = B.cols;
    C.rowPtr.assign(toSize(A.rows) + 1, 0);

    // Symbolic pass: number of distinct columns per row of C.
    std::vector<Index> marker(toSize(B.cols), -1);
    for (Index i = 0; i < A.rows; ++i) {
        Offset count = 0;
        for (Offset ka = A.rowPtr[toSize(i)]; ka < A.rowPtr[toSize(i) + 1]; ++ka) {
            const Index j = A.col[toSize(ka)];
            for (Offset kb = B.rowPtr[toSize(j)]; kb < B.rowPtr[toSize(j) + 1]; ++kb) {
                const Index c = B.col[toSize(kb)];
                if (marker[toSize(c)] != i) {
                    marker[toSize(c)] = i;
                    ++count;
                }
            }
        }
        C.rowPtr[toSize(i) + 1] = C.rowPtr[toSize(i)] + count;
    }
    C.col.resize(toSize(C.nnz()));
    C.val.resize(toSize(C.nnz()));

    // Numeric pass.
    std::fill(marker.begin(), marker.end(), -1);
    std::vector<double> acc(toSize(B.cols), 0.0);
    for (Index i = 0; i < A.rows; ++i) {
        const Offset start = C.rowPtr[toSize(i)];
        Offset len = 0;
        for (Offset ka = A.rowPtr[toSize(i)]; ka < A.rowPtr[toSize(i) + 1]; ++ka) {
            const Index j = A.col[toSize(ka)];
            const double a = A.val[toSize(ka)];
            for (Offset kb = B.rowPtr[toSize(j)]; kb < B.rowPtr[toSize(j) + 1]; ++kb) {
                const Index c = B.col[toSize(kb)];
                const double product = a * B.val[toSize(kb)];
                if (marker[toSize(c)] != i) {
                    marker[toSize(c)] = i;
                    C.col[toSize(start + len)] = c;
                    ++len;
                    acc[toSize(c)] = product;
                } else {
                    acc[toSize(c)] += product;
                }
            }
        }
        const auto first = C.col.begin() + start;
        std::sort(first, first + len);
        for (Offset k = start; k < start + len; ++k) {
            C.val[toSize(k)] = acc[toSize(C.col[toSize(k)])];
        }
    }
    return C;
}

namespace {

// Per-thread work arrays of the parallel product. A stamp (call number in the high 32 bits,
// row in the low 32) marks the columns already seen in the current row, so the arrays never
// need clearing between rows or calls.
struct ProductScratch {
    std::vector<std::int64_t> marker;
    std::vector<double> acc;
};

ProductScratch& scratchFor(Index cols) {
    thread_local ProductScratch s;
    if (s.marker.size() < toSize(cols)) {
        s.marker.resize(toSize(cols), -1);
        s.acc.resize(toSize(cols), 0.0);
    }
    return s;
}

std::atomic<std::int64_t> productCall{0};

} // namespace

Result<CsrMatrix> multiply(KernelPool& pool, const CsrMatrix& A, const CsrMatrix& B) {
    if (A.cols != B.rows) {
        return multiply(A, B); // same error message
    }
    CsrMatrix C;
    C.rows = A.rows;
    C.cols = B.cols;
    C.rowPtr.assign(toSize(A.rows) + 1, 0);

    const std::int64_t symbolicCall = (productCall.fetch_add(1) + 1) << 32;
    pool.parallelFor(A.rows, [&](Index begin, Index end) {
        ProductScratch& s = scratchFor(B.cols);
        for (Index i = begin; i < end; ++i) {
            const std::int64_t stamp = symbolicCall | static_cast<std::int64_t>(i);
            Offset count = 0;
            for (Offset ka = A.rowPtr[toSize(i)]; ka < A.rowPtr[toSize(i) + 1]; ++ka) {
                const Index j = A.col[toSize(ka)];
                for (Offset kb = B.rowPtr[toSize(j)]; kb < B.rowPtr[toSize(j) + 1]; ++kb) {
                    const Index c = B.col[toSize(kb)];
                    if (s.marker[toSize(c)] != stamp) {
                        s.marker[toSize(c)] = stamp;
                        ++count;
                    }
                }
            }
            C.rowPtr[toSize(i) + 1] = count;
        }
    });
    for (std::size_t i = 0; i < toSize(A.rows); ++i) {
        C.rowPtr[i + 1] += C.rowPtr[i];
    }
    C.col.resize(toSize(C.nnz()));
    C.val.resize(toSize(C.nnz()));

    const std::int64_t numericCall = (productCall.fetch_add(1) + 1) << 32;
    pool.parallelFor(A.rows, [&](Index begin, Index end) {
        ProductScratch& s = scratchFor(B.cols);
        for (Index i = begin; i < end; ++i) {
            const std::int64_t stamp = numericCall | static_cast<std::int64_t>(i);
            const Offset start = C.rowPtr[toSize(i)];
            Offset len = 0;
            for (Offset ka = A.rowPtr[toSize(i)]; ka < A.rowPtr[toSize(i) + 1]; ++ka) {
                const Index j = A.col[toSize(ka)];
                const double a = A.val[toSize(ka)];
                for (Offset kb = B.rowPtr[toSize(j)]; kb < B.rowPtr[toSize(j) + 1]; ++kb) {
                    const Index c = B.col[toSize(kb)];
                    const double product = a * B.val[toSize(kb)];
                    if (s.marker[toSize(c)] != stamp) {
                        s.marker[toSize(c)] = stamp;
                        C.col[toSize(start + len)] = c;
                        ++len;
                        s.acc[toSize(c)] = product;
                    } else {
                        s.acc[toSize(c)] += product;
                    }
                }
            }
            const auto first = C.col.begin() + start;
            std::sort(first, first + len);
            for (Offset k = start; k < start + len; ++k) {
                C.val[toSize(k)] = s.acc[toSize(C.col[toSize(k)])];
            }
        }
    });
    return C;
}

} // namespace uniforge::sparse
