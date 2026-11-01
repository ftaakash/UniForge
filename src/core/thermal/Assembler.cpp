#include "core/thermal/Assembler.hpp"

#include <algorithm>

#include "core/thermal/ElementMatrices.hpp"
#include "core/thermal/Mesh1D.hpp"

namespace uniforge::thermal {

void TridiagonalSystem::reset(std::size_t n) {
    for (std::vector<double>* v : {&a, &b, &c, &f}) {
        v->assign(n, 0.0);
    }
}

void assemble(const RodProblem& problem, TridiagonalSystem& system) {
    const Mesh1D mesh{problem.L, problem.elements};
    system.reset(mesh.nodeCount());

    // Uniform mesh: every element has the same length, so Ke and fe are computed once.
    const double h = mesh.h();
    const Matrix2 ke = elementStiffness(problem.k, problem.A, h);
    const Vector2 fe = elementLoad(problem.q, problem.A, h);

    const auto n = static_cast<std::size_t>(problem.elements);
    for (std::size_t e = 0; e < n; ++e) {
        // Element e couples global nodes e and e + 1.
        system.b[e] += ke[0][0];
        system.c[e] += ke[0][1];
        system.a[e + 1] += ke[1][0];
        system.b[e + 1] += ke[1][1];
        system.f[e] += fe[0];
        system.f[e + 1] += fe[1];
    }
}

} // namespace uniforge::thermal
