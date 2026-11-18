#include "core/sparse/SparseOps.hpp"

#include <algorithm>
#include <string>
#include <vector>

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

} // namespace uniforge::sparse
