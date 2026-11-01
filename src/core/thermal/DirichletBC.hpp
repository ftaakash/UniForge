#pragma once

#include "core/thermal/Assembler.hpp"

namespace uniforge::thermal {

// One row of the assembled system before boundary conditions were applied. The reaction at
// that node (the nodal heat the boundary must supply, in W) is
//     r = lower·T[i−1] + diag·T[i] + upper·T[i+1] − rhs.
struct UnconstrainedRow {
    double lower = 0.0;
    double diag = 0.0;
    double upper = 0.0;
    double rhs = 0.0;
};

struct EliminatedRows {
    UnconstrainedRow first; // node 0
    UnconstrainedRow last;  // node N
};

// Imposes T[0] = T0 and T[N] = TL by symmetric elimination (spec §7.3):
//     f[1]   −= a[1]·T0;    a[1]   = 0;   b[0] = 1;   c[0]   = 0;   f[0] = T0
//     f[N−1] −= c[N−1]·TL;  c[N−1] = 0;   b[N] = 1;   a[N]   = 0;   f[N] = TL
// The known values move to the right-hand side of the neighbouring rows, so the remaining
// matrix stays symmetric (and positive definite) and the boundary rows become identities, which
// makes T[0] == T0 and T[N] == TL hold exactly after the solve.
// Returns rows 0 and N as they were before elimination, for computing the end heat flows.
// Pre-condition: system.size() >= 2.
EliminatedRows applyDirichlet(TridiagonalSystem& system, double T0, double TL);

} // namespace uniforge::thermal
