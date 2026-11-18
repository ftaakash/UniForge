#include "core/sparse/CsrMatrix.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace uniforge::sparse {

namespace {

Error invalid(std::string message) {
    return makeError(ErrorCode::InvalidInput, std::move(message),
                     "This is a programming error in the caller; the matrix was not built.");
}

Result<void> checkSizes(Index rows, Index cols) {
    if (rows < 0 || cols < 0) {
        return invalid("Matrix size " + std::to_string(rows) + " × " + std::to_string(cols) +
                       " is negative.");
    }
    return {};
}

Result<void> validateStructure(Index rows, Index cols, const std::vector<Offset>& rowPtr,
                               const std::vector<Index>& col) {
    if (auto r = checkSizes(rows, cols); !r) {
        return r;
    }
    if (rowPtr.size() != toSize(rows) + 1) {
        return invalid("rowPtr has " + std::to_string(rowPtr.size()) + " entries, expected " +
                       std::to_string(toSize(rows) + 1) + ".");
    }
    if (rowPtr.front() != 0) {
        return invalid("rowPtr[0] is " + std::to_string(rowPtr.front()) + ", expected 0.");
    }
    if (rowPtr.back() < 0 || toSize(rowPtr.back()) != col.size()) {
        return invalid("rowPtr ends at " + std::to_string(rowPtr.back()) + " but there are " +
                       std::to_string(col.size()) + " column entries.");
    }
    for (Index r = 0; r < rows; ++r) {
        const Offset begin = rowPtr[toSize(r)];
        const Offset end = rowPtr[toSize(r) + 1];
        if (end < begin) {
            return invalid("rowPtr decreases at row " + std::to_string(r) + ".");
        }
        for (Offset k = begin; k < end; ++k) {
            const Index c = col[toSize(k)];
            if (c < 0 || c >= cols) {
                return invalid("Row " + std::to_string(r) + " has column " + std::to_string(c) +
                               ", outside 0 … " + std::to_string(cols - 1) + ".");
            }
            if (k > begin && c <= col[toSize(k - 1)]) {
                return invalid("Row " + std::to_string(r) +
                               " has columns that are not strictly increasing (at column " +
                               std::to_string(c) + ").");
            }
        }
    }
    return {};
}

} // namespace

Offset CsrMatrix::find(Index r, Index c) const noexcept {
    if (r < 0 || r >= rows) {
        return -1;
    }
    const auto first = col.begin() + rowPtr[toSize(r)];
    const auto last = col.begin() + rowPtr[toSize(r) + 1];
    const auto it = std::lower_bound(first, last, c);
    if (it == last || *it != c) {
        return -1;
    }
    return static_cast<Offset>(it - col.begin());
}

double CsrMatrix::at(Index r, Index c) const noexcept {
    const Offset k = find(r, c);
    return k < 0 ? 0.0 : val[toSize(k)];
}

std::size_t CsrMatrix::bytes() const noexcept {
    return rowPtr.capacity() * sizeof(Offset) + col.capacity() * sizeof(Index) +
           val.capacity() * sizeof(double);
}

CsrMatrix makeMatrix(CsrPattern pattern) {
    CsrMatrix m;
    m.rows = pattern.rows;
    m.cols = pattern.cols;
    m.rowPtr = std::move(pattern.rowPtr);
    m.col = std::move(pattern.col);
    m.val.assign(m.col.size(), 0.0);
    return m;
}

Result<CsrMatrix> fromTriplets(Index rows, Index cols, std::vector<Triplet> triplets) {
    if (auto r = checkSizes(rows, cols); !r) {
        return std::move(r).error();
    }
    for (const Triplet& t : triplets) {
        if (t.row < 0 || t.row >= rows || t.col < 0 || t.col >= cols) {
            return invalid("Entry (" + std::to_string(t.row) + ", " + std::to_string(t.col) +
                           ") is outside a " + std::to_string(rows) + " × " + std::to_string(cols) +
                           " matrix.");
        }
        if (!std::isfinite(t.value)) {
            return makeError(ErrorCode::NonFinite,
                             "Entry (" + std::to_string(t.row) + ", " + std::to_string(t.col) +
                                 ") is not a finite number.",
                             "Check the inputs that produced this matrix.");
        }
    }
    // Stable sort keeps the input order of duplicates, so their sum is deterministic.
    std::stable_sort(triplets.begin(), triplets.end(), [](const Triplet& a, const Triplet& b) {
        return a.row != b.row ? a.row < b.row : a.col < b.col;
    });

    CsrMatrix m;
    m.rows = rows;
    m.cols = cols;
    m.rowPtr.assign(toSize(rows) + 1, 0);
    m.col.reserve(triplets.size());
    m.val.reserve(triplets.size());
    for (std::size_t i = 0; i < triplets.size(); ++i) {
        const Triplet& t = triplets[i];
        const bool sameAsPrevious =
            i > 0 && triplets[i - 1].row == t.row && triplets[i - 1].col == t.col;
        if (sameAsPrevious) {
            m.val.back() += t.value;
        } else {
            m.col.push_back(t.col);
            m.val.push_back(t.value);
            ++m.rowPtr[toSize(t.row) + 1];
        }
    }
    for (std::size_t r = 0; r < toSize(rows); ++r) {
        m.rowPtr[r + 1] += m.rowPtr[r];
    }
    return m;
}

Result<CsrPattern> buildPattern(Index rows, Index cols, const RowColumnsFn& rowColumns) {
    if (auto r = checkSizes(rows, cols); !r) {
        return std::move(r).error();
    }
    CsrPattern p;
    p.rows = rows;
    p.cols = cols;
    p.rowPtr.assign(toSize(rows) + 1, 0);
    std::vector<Index> buffer;
    for (Index r = 0; r < rows; ++r) {
        buffer.clear();
        rowColumns(r, buffer);
        std::sort(buffer.begin(), buffer.end());
        buffer.erase(std::unique(buffer.begin(), buffer.end()), buffer.end());
        if (!buffer.empty() && (buffer.front() < 0 || buffer.back() >= cols)) {
            const Index bad = buffer.front() < 0 ? buffer.front() : buffer.back();
            return invalid("Row " + std::to_string(r) + " lists column " + std::to_string(bad) +
                           ", outside 0 … " + std::to_string(cols - 1) + ".");
        }
        p.col.insert(p.col.end(), buffer.begin(), buffer.end());
        p.rowPtr[toSize(r) + 1] = static_cast<Offset>(p.col.size());
    }
    return p;
}

Result<void> validate(const CsrPattern& pattern) {
    return validateStructure(pattern.rows, pattern.cols, pattern.rowPtr, pattern.col);
}

Result<void> validate(const CsrMatrix& matrix) {
    if (auto r = validateStructure(matrix.rows, matrix.cols, matrix.rowPtr, matrix.col); !r) {
        return r;
    }
    if (matrix.val.size() != matrix.col.size()) {
        return invalid("The matrix has " + std::to_string(matrix.val.size()) + " values for " +
                       std::to_string(matrix.col.size()) + " entries.");
    }
    for (std::size_t k = 0; k < matrix.val.size(); ++k) {
        if (!std::isfinite(matrix.val[k])) {
            return makeError(ErrorCode::NonFinite,
                             "Matrix entry " + std::to_string(k) + " is not a finite number.",
                             "Check the inputs that produced this matrix.");
        }
    }
    return {};
}

bool isSymmetric(const CsrMatrix& matrix, double tol) {
    if (matrix.rows != matrix.cols) {
        return false;
    }
    for (Index r = 0; r < matrix.rows; ++r) {
        for (Offset k = matrix.rowPtr[toSize(r)]; k < matrix.rowPtr[toSize(r) + 1]; ++k) {
            const Index c = matrix.col[toSize(k)];
            const double a = matrix.val[toSize(k)];
            const double b = matrix.at(c, r);
            if (std::fabs(a - b) > tol * std::max(std::fabs(a), std::fabs(b))) {
                return false;
            }
        }
    }
    return true;
}

std::vector<double> toDense(const CsrMatrix& matrix) {
    std::vector<double> dense(toSize(matrix.rows) * toSize(matrix.cols), 0.0);
    for (Index r = 0; r < matrix.rows; ++r) {
        for (Offset k = matrix.rowPtr[toSize(r)]; k < matrix.rowPtr[toSize(r) + 1]; ++k) {
            dense[toSize(r) * toSize(matrix.cols) + toSize(matrix.col[toSize(k)])] =
                matrix.val[toSize(k)];
        }
    }
    return dense;
}

} // namespace uniforge::sparse
