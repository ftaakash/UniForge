#include "core/thermal/ThomasSolver.hpp"

#include <cmath>
#include <string>

namespace uniforge::thermal {

namespace {

Error badPivot(std::size_t row) {
    return makeError(ErrorCode::Internal,
                     "Tridiagonal solve hit a zero or non-finite pivot in row " +
                         std::to_string(row) + ".",
                     "The system is singular; check the problem parameters.");
}

} // namespace

Result<void> solveThomas(const TridiagonalSystem& s, std::vector<double>& scratch,
                         std::vector<double>& x) {
    const std::size_t n = s.size();
    if (n == 0 || s.a.size() != n || s.c.size() != n || s.f.size() != n) {
        return makeError(ErrorCode::Internal, "Tridiagonal system is empty or inconsistent.",
                         "This is a bug in the assembly code; please report it.");
    }
    scratch.resize(n);
    x.resize(n);

    // Forward sweep: eliminate the sub-diagonal. scratch = c', x holds d' for now.
    double m = s.b[0];
    if (m == 0.0 || !std::isfinite(m)) {
        return badPivot(0);
    }
    scratch[0] = s.c[0] / m;
    x[0] = s.f[0] / m;
    for (std::size_t i = 1; i < n; ++i) {
        m = s.b[i] - s.a[i] * scratch[i - 1];
        if (m == 0.0 || !std::isfinite(m)) {
            return badPivot(i);
        }
        scratch[i] = s.c[i] / m;
        x[i] = (s.f[i] - s.a[i] * x[i - 1]) / m;
    }

    // Back substitution.
    for (std::size_t i = n - 1; i-- > 0;) {
        x[i] -= scratch[i] * x[i + 1];
    }
    return {};
}

} // namespace uniforge::thermal
