#include "core/fem3d/Design.hpp"

#include <cmath>
#include <string>
#include <utility>

namespace uniforge::fem3d {

namespace {

// Boxes closer than this (m) count as touching, not overlapping. The smallest V2 feature is
// the 0.1 mm TIM, eight orders of magnitude larger.
constexpr double kTouchTolerance = 1e-12;

Error invalid(const std::string& message) {
    return makeError(ErrorCode::InvalidInput, message,
                     "Fix the design parameters; nothing was meshed.");
}

bool finite(double v) {
    return std::isfinite(v);
}

bool overlap(const Box& a, const Box& b) {
    for (int ax = 0; ax < 3; ++ax) {
        const auto i = static_cast<std::size_t>(ax);
        if (a.min[i] >= b.max[i] - kTouchTolerance || b.min[i] >= a.max[i] - kTouchTolerance) {
            return false;
        }
    }
    return true;
}

std::array<FaceBc, kSides> allFaces(const FaceBc& bc) {
    std::array<FaceBc, kSides> f{};
    f.fill(bc);
    return f;
}

} // namespace

double Design::volume() const noexcept {
    double v = 0.0;
    for (const Box& b : boxes) {
        v += b.volume();
    }
    return v;
}

double Design::heatGenerated() const noexcept {
    double p = 0.0;
    for (const Box& b : boxes) {
        if (b.material >= 0 && static_cast<std::size_t>(b.material) < materials.size()) {
            p += materials[static_cast<std::size_t>(b.material)].q * b.volume();
        }
    }
    return p;
}

Result<void> validate(const Design& design) {
    if (design.boxes.empty()) {
        return invalid("Design '" + design.name + "' has no boxes.");
    }
    for (const Material& m : design.materials) {
        if (!finite(m.k) || !finite(m.q)) {
            return makeError(ErrorCode::NonFinite,
                             "Material '" + m.name + "' has a non-finite property.",
                             "Fix the design parameters; nothing was meshed.");
        }
        if (m.k <= 0.0) {
            return invalid("Material '" + m.name + "' has conductivity k = " + std::to_string(m.k) +
                           " W/(m·K); it must be positive.");
        }
    }
    for (const Box& b : design.boxes) {
        for (std::size_t ax = 0; ax < 3; ++ax) {
            if (!finite(b.min[ax]) || !finite(b.max[ax])) {
                return makeError(ErrorCode::NonFinite,
                                 "Box '" + b.name + "' has a non-finite coordinate.",
                                 "Fix the design parameters; nothing was meshed.");
            }
            if (!(b.max[ax] > b.min[ax])) {
                return invalid("Box '" + b.name + "' has no extent along axis " +
                               std::to_string(ax) + ".");
            }
        }
        if (b.material < 0 || static_cast<std::size_t>(b.material) >= design.materials.size()) {
            return invalid("Box '" + b.name + "' refers to material " + std::to_string(b.material) +
                           ", which does not exist.");
        }
        for (const FaceBc& f : b.faces) {
            if (!finite(f.h) || !finite(f.tInf) || !finite(f.t)) {
                return makeError(ErrorCode::NonFinite,
                                 "Box '" + b.name + "' has a non-finite boundary value.",
                                 "Fix the design parameters; nothing was meshed.");
            }
            if (f.kind == BcKind::Convection && f.h < 0.0) {
                return invalid("Box '" + b.name + "' has a negative film coefficient.");
            }
        }
    }
    for (std::size_t i = 0; i < design.boxes.size(); ++i) {
        for (std::size_t j = i + 1; j < design.boxes.size(); ++j) {
            if (overlap(design.boxes[i], design.boxes[j])) {
                return invalid("Boxes '" + design.boxes[i].name + "' and '" + design.boxes[j].name +
                               "' overlap.");
            }
        }
    }
    for (const FixedLayer& l : design.fixedLayers) {
        if (l.axis < 0 || l.axis > 2 || !finite(l.lo) || !finite(l.hi) || !(l.hi > l.lo)) {
            return invalid("Design '" + design.name + "' has an invalid fixed layer.");
        }
    }
    return {};
}

Design makeV1Bar() {
    Design d;
    d.name = "v1 bar";
    d.ladderLevel = "L0a";
    d.materials = {{"bar", 10.0, 1e4}};
    Box b;
    b.name = "bar";
    b.min = {0.0, -0.005, -0.005};
    b.max = {1.0, 0.005, 0.005};
    b.material = 0;
    b.faces = allFaces(FaceBc::insulated());
    b.faces[XMin] = FaceBc::dirichlet(100.0);
    b.faces[XMax] = FaceBc::dirichlet(20.0);
    d.boxes = {b};
    return d;
}

Design makeCube() {
    Design d = makeV1Bar();
    d.name = "cube";
    d.ladderLevel = "L0b";
    d.boxes[0].name = "cube";
    const double a = std::cbrt(1e-4); // same volume as the bar
    d.boxes[0].min = {0.0, -a / 2.0, -a / 2.0};
    d.boxes[0].max = {a, a / 2.0, a / 2.0};
    return d;
}

namespace {

// Base plate, die and (optionally) TIM; fins or pins are added by the callers.
Design heatSinkBody(const HeatSinkParams& p, std::string name, std::string level) {
    Design d;
    d.name = std::move(name);
    d.ladderLevel = std::move(level);
    const double dieVolume = p.dieX * p.dieY * p.dieZ;
    d.materials = {{"aluminium", p.kAluminium, 0.0},
                   {"silicon", p.kSilicon, dieVolume > 0.0 ? p.power / dieVolume : 0.0},
                   {"TIM", p.kTim, 0.0}};
    const FaceBc air = FaceBc::convection(p.h, p.tInf);

    Box base;
    base.name = "base";
    base.min = {0.0, 0.0, 0.0};
    base.max = {p.baseX, p.baseY, p.baseZ};
    base.material = 0;
    base.faces = allFaces(air);
    base.faces[ZMin] = FaceBc::insulated(); // mounted on the board
    d.boxes.push_back(base);

    const double x0 = (p.baseX - p.dieX) / 2.0;
    const double y0 = (p.baseY - p.dieY) / 2.0;
    double zTop = 0.0;
    if (p.withTim) {
        Box tim;
        tim.name = "TIM";
        tim.min = {x0, y0, -p.timThickness};
        tim.max = {x0 + p.dieX, y0 + p.dieY, 0.0};
        tim.material = 2;
        tim.faces = allFaces(FaceBc::insulated());
        d.boxes.push_back(tim);
        d.fixedLayers.push_back({2, -p.timThickness, 0.0});
        zTop = -p.timThickness;
    }
    Box die;
    die.name = "die";
    die.min = {x0, y0, zTop - p.dieZ};
    die.max = {x0 + p.dieX, y0 + p.dieY, zTop};
    die.material = 1;
    die.faces = allFaces(FaceBc::insulated()); // bottom on the board, thin sides neglected
    d.boxes.push_back(die);
    return d;
}

} // namespace

Design makeHeatSink(const HeatSinkParams& p) {
    std::string level = "custom";
    if (!p.withFins && !p.withTim) {
        level = "L1";
    } else if (p.withFins && !p.withTim) {
        level = "L2";
    } else if (p.withFins && p.withTim) {
        level = "L3";
    }
    Design d = heatSinkBody(p, p.withFins && p.withTim ? "HS-7" : "heat sink " + level, level);
    if (p.withFins) {
        const FaceBc air = FaceBc::convection(p.h, p.tInf);
        for (int i = 0; i < p.finCount; ++i) {
            Box fin;
            fin.name = "fin " + std::to_string(i + 1);
            const double x = p.finFirstX + i * p.finPitch;
            fin.min = {x, 0.0, p.baseZ};
            fin.max = {x + p.finThickness, p.baseY, p.baseZ + p.finHeight};
            fin.material = 0;
            fin.faces = allFaces(air);
            fin.faces[ZMin] = FaceBc::insulated(); // fully on the base: internal
            d.boxes.push_back(fin);
        }
    }
    return d;
}

Design makePlate(HeatSinkParams params) {
    params.withFins = false;
    params.withTim = false;
    return makeHeatSink(params);
}

Design makeFinnedPlate(HeatSinkParams params) {
    params.withFins = true;
    params.withTim = false;
    return makeHeatSink(params);
}

Design makeHS7(HeatSinkParams params) {
    params.withFins = true;
    params.withTim = true;
    return makeHeatSink(params);
}

Design makePinFin(HeatSinkParams params, PinFinParams pins) {
    Design d = heatSinkBody(params, "pin fin", "L4");
    const FaceBc air = FaceBc::convection(params.h, params.tInf);
    const double pitchX = params.baseX / pins.pinsX;
    const double pitchY = params.baseY / pins.pinsY;
    for (int j = 0; j < pins.pinsY; ++j) {
        for (int i = 0; i < pins.pinsX; ++i) {
            Box pin;
            pin.name = "pin " + std::to_string(i + 1) + "," + std::to_string(j + 1);
            const double cx = (i + 0.5) * pitchX;
            const double cy = (j + 0.5) * pitchY;
            pin.min = {cx - pins.pinSide / 2.0, cy - pins.pinSide / 2.0, params.baseZ};
            pin.max = {cx + pins.pinSide / 2.0, cy + pins.pinSide / 2.0,
                       params.baseZ + params.finHeight};
            pin.material = 0;
            pin.faces = allFaces(air);
            pin.faces[ZMin] = FaceBc::insulated();
            d.boxes.push_back(pin);
        }
    }
    return d;
}

} // namespace uniforge::fem3d
