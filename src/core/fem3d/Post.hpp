#pragma once

#include <array>
#include <cstdint>

#include "core/common/Result.hpp"
#include "core/fem3d/Assembly.hpp"

namespace uniforge::fem3d {

// Global heat balance of a solution (build spec §3.5): heat generated = heat convected + heat
// leaving through prescribed-temperature faces. All in W.
struct EnergyAudit {
    double heatIn = 0.0;           // ∫ q dV
    double heatConvected = 0.0;    // Σ over convective faces ∫ h (T − T∞) dA
    double heatOutDirichlet = 0.0; // −Σ reactions: heat the prescribed faces carry away
    double imbalance = 0.0;        // heatIn − heatConvected − heatOutDirichlet
    double relImbalance = 0.0; // |imbalance| / max(|heatIn|, |heatConvected| + |heatOutDirichlet|)
};

// Recomputes every term from the face data (the same quadrature as the assembly, options too),
// so an error in the assembly or the boundary bookkeeping shows up as an imbalance.
[[nodiscard]] EnergyAudit energyAudit(const Design& design, const HexMesh& mesh,
                                      const LinearSystem& system, const Vector& T,
                                      const AssemblyOptions& options = {});

// Reaction r_d = K_d·T − f_d of each Dirichlet node (unconstrained rows), in the order of
// system.dirichletNodes. The heat a prescribed node removes from the body is −r_d.
[[nodiscard]] Vector reactions(const LinearSystem& system, const Vector& T);

// Highest nodal temperature and where it is.
struct Peak {
    double t = 0.0;
    std::array<double, 3> at{};
    std::int32_t node = -1;
};
[[nodiscard]] Peak peakTemperature(const HexMesh& mesh, const Vector& T);

// Trilinear interpolation of T at a point inside an active cell (on a cell boundary any
// neighbouring active cell gives the same value). InvalidInput outside the body.
[[nodiscard]] Result<double> sampleTemperature(const HexMesh& mesh, const Vector& T, double x,
                                               double y, double z);

// Error against an exact solution: L2 norm by 2×2×2 Gauss over every active cell, and the
// largest nodal error.
struct ErrorNorms {
    double l2 = 0.0;
    double maxNodal = 0.0;
    double volume = 0.0; // Σ cell volumes (for an RMS value: l2 / sqrt(volume))
};
[[nodiscard]] ErrorNorms errorNorms(const HexMesh& mesh, const Vector& T, const ScalarField& exact);

} // namespace uniforge::fem3d
