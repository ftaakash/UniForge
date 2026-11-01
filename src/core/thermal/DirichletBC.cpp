#include "core/thermal/DirichletBC.hpp"

namespace uniforge::thermal {

EliminatedRows applyDirichlet(TridiagonalSystem& s, double T0, double TL) {
    const std::size_t N = s.size() - 1;

    // Save both boundary rows first: for N = 1 the left elimination touches row N.
    EliminatedRows saved;
    saved.first = {0.0, s.b[0], s.c[0], s.f[0]};
    saved.last = {s.a[N], s.b[N], 0.0, s.f[N]};

    // Left end, node 0.
    s.f[1] -= s.a[1] * T0;
    s.a[1] = 0.0;
    s.b[0] = 1.0;
    s.c[0] = 0.0;
    s.f[0] = T0;

    // Right end, node N (mirrored).
    s.f[N - 1] -= s.c[N - 1] * TL;
    s.c[N - 1] = 0.0;
    s.b[N] = 1.0;
    s.a[N] = 0.0;
    s.f[N] = TL;

    // For N = 1 the left step wrote f[1] and the right step then overwrote it with TL, and the
    // right step's f[0] update used c[0] = 0, so both rows end up as plain identities.
    return saved;
}

} // namespace uniforge::thermal
