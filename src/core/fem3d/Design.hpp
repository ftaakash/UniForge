#pragma once

#include <array>
#include <string>
#include <vector>

#include "core/common/Result.hpp"

namespace uniforge::fem3d {

// A V2 object is a union of axis-aligned boxes (build spec §3.1, §6.1). Lengths in metres,
// conductivity in W/(m·K), volumetric heat generation in W/m³, temperatures in °C, film
// coefficients in W/(m²·K).

enum class BcKind { Insulated, Convection, Dirichlet };

struct FaceBc {
    BcKind kind = BcKind::Insulated;
    double h = 0.0;    // Convection: film coefficient
    double tInf = 0.0; // Convection: ambient temperature
    double t = 0.0;    // Dirichlet: prescribed temperature

    [[nodiscard]] static FaceBc insulated() { return {}; }
    [[nodiscard]] static FaceBc convection(double film, double ambient) {
        return {BcKind::Convection, film, ambient, 0.0};
    }
    [[nodiscard]] static FaceBc dirichlet(double temperature) {
        return {BcKind::Dirichlet, 0.0, 0.0, temperature};
    }
};

// Box faces in this order everywhere (also the `side` of a mesh boundary face).
enum Side : int { XMin = 0, XMax = 1, YMin = 2, YMax = 3, ZMin = 4, ZMax = 5 };
inline constexpr int kSides = 6;

struct Material {
    std::string name;
    double k = 0.0; // thermal conductivity
    double q = 0.0; // uniform volumetric heat generation
};

struct Box {
    std::string name;
    std::array<double, 3> min{};
    std::array<double, 3> max{};
    int material = 0;
    // Boundary condition of each face where it is exposed. Parts of a face that touch another
    // box are internal and ignore it.
    std::array<FaceBc, kSides> faces{};

    [[nodiscard]] double volume() const noexcept {
        return (max[0] - min[0]) * (max[1] - min[1]) * (max[2] - min[2]);
    }
};

// A layer whose number of cells across `axis` is fixed by MeshSpec::fixedLayerCells instead of
// the target spacing (build spec §3.4: the TIM gets 1 or 2 cells through its thickness).
struct FixedLayer {
    int axis = 2;
    double lo = 0.0;
    double hi = 0.0;
};

struct Design {
    std::string name;
    std::string ladderLevel; // "L0a", "L0b", "L1" … "L4" (build spec §3.1)
    std::vector<Material> materials;
    std::vector<Box> boxes;
    std::vector<FixedLayer> fixedLayers;

    // Plain sums used by the checks.
    [[nodiscard]] double volume() const noexcept;
    [[nodiscard]] double heatGenerated() const noexcept; // Σ q·V over boxes, W
};

// Checks: at least one box; finite numbers; every box has positive extent in all three axes;
// material indices valid; k > 0; h ≥ 0 for convection; no two boxes overlap (touching is fine);
// fixed layers on a valid axis with lo < hi. InvalidInput or NonFinite with the box named.
[[nodiscard]] Result<void> validate(const Design& design);

// ---- The V2 objects (build spec §3.1, §3.2) ------------------------------------------------

// L0a: the v1 bar, 1 m × 10 mm × 10 mm, k = 10, q = 1e4, T = 100 °C at x = 0 and 20 °C at
// x = 1 m, other faces insulated.
[[nodiscard]] Design makeV1Bar();

// L0b: a cube with the bar's volume (edge (1e-4)^(1/3) m ≈ 46.4 mm), same k, q and end
// temperatures on the x faces, other faces insulated.
[[nodiscard]] Design makeCube();

// Parameters of the finned heat sink (build spec §3.2). Values marked [EST] in the spec are
// chosen inputs, not measured data: P, h, k of the TIM.
struct HeatSinkParams {
    // Base plate (origin at its lower front-left corner)
    double baseX = 0.060, baseY = 0.040, baseZ = 0.004;
    // Straight fins along y, on top of the base
    int finCount = 7;
    double finThickness = 0.002, finHeight = 0.026, finPitch = 0.009, finFirstX = 0.003;
    // Silicon die under the base, centred in x and y
    double dieX = 0.020, dieY = 0.020, dieZ = 0.0005;
    // Thermal interface layer between die and base (same footprint as the die)
    double timThickness = 0.0001;
    // Materials, W/(m·K) [VERIFY handbook values before the report]
    double kAluminium = 200.0, kTim = 3.0, kSilicon = 150.0;
    // Load: total die power (W), film coefficient (W/(m²·K)), ambient (°C)
    double power = 30.0, h = 25.0, tInf = 25.0;
    // Which parts exist (the complexity ladder)
    bool withFins = true;
    bool withTim = true;
};

// L1 plate (base + die), L2 fins (L1 + fins), L3 HS-7 (L2 + TIM): the ladder of build spec §3.1.
// The die sits directly under the base when there is no TIM.
[[nodiscard]] Design makeHeatSink(const HeatSinkParams& params);
[[nodiscard]] Design makePlate(HeatSinkParams params = {});
[[nodiscard]] Design makeFinnedPlate(HeatSinkParams params = {});
[[nodiscard]] Design makeHS7(HeatSinkParams params = {});

// L4: pin-fin variant: square pins in a regular grid instead of straight fins [EST layout].
struct PinFinParams {
    int pinsX = 8, pinsY = 6;
    double pinSide = 0.002;
};
[[nodiscard]] Design makePinFin(HeatSinkParams params = {}, PinFinParams pins = {});

} // namespace uniforge::fem3d
