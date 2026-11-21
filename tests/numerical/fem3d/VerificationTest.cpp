// Verification ladder of the 3D FEA (build spec §3.6), solved with PCG-J:
//   patch test (exact for harmonic trilinear fields), the v1 bar in 3D (188.2 °C at 0.42 m,
//   0.42 / 0.58 W through the ends), a manufactured solution with convection on every face
//   (order 2), a single fin against 1D fin theory, and the energy balance of HS-7.

#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>

#include <gtest/gtest.h>

#include "core/fem3d/Assembly.hpp"
#include "core/fem3d/Post.hpp"
#include "core/solvers/krylov/Pcg.hpp"

using namespace uniforge::fem3d;
using uniforge::solvers::JacobiPreconditioner;
using uniforge::solvers::Pcg;
using uniforge::solvers::SolveControl;
using uniforge::solvers::SolveReport;
using uniforge::solvers::SolverKind;
using uniforge::solvers::SolveStatus;
using uniforge::sparse::KernelPool;

namespace {

struct Solved {
    HexMesh mesh;
    LinearSystem system;
    Vector T;
    SolveReport report;
};

Solved solve(const Design& d, double s, const AssemblyOptions& o = {}, double relTol = 1e-13) {
    Solved out;
    MeshSpec spec;
    spec.targetSpacing = s;
    auto m = buildMesh(d, spec);
    EXPECT_TRUE(m.ok()) << (m.ok() ? "" : m.error().message);
    out.mesh = std::move(m).value();
    auto sys = assemble(d, out.mesh, o);
    EXPECT_TRUE(sys.ok()) << (sys.ok() ? "" : sys.error().message);
    out.system = std::move(sys).value();

    KernelPool pool(4);
    Pcg pcg(SolverKind::PcgJacobi, std::make_unique<JacobiPreconditioner>());
    auto su = pcg.setup(out.system.K, {}, pool, {});
    EXPECT_TRUE(su.ok() && su.value().status == SolveStatus::Converged);
    SolveControl c;
    c.relTol = relTol;
    c.maxIterations = 200000;
    c.stagnationWindow = 0;
    c.residualSumMask = &out.system.freeMask;
    out.T.assign(out.system.f.size(), 0.0);
    auto r = pcg.solve(out.system.f, out.T, c);
    EXPECT_TRUE(r.ok());
    out.report = r.value();
    EXPECT_EQ(out.report.status, SolveStatus::Converged) << out.report.detail;
    return out;
}

// A single-material box with the same boundary condition on every face.
Design boxDesign(double lx, double ly, double lz, double k, const FaceBc& bc) {
    Design d;
    d.name = "box";
    d.materials = {{"m", k, 0.0}};
    Box b;
    b.name = "box";
    b.min = {0.0, 0.0, 0.0};
    b.max = {lx, ly, lz};
    b.faces.fill(bc);
    d.boxes = {b};
    return d;
}

} // namespace

// ---- Patch test -------------------------------------------------------------------------------

TEST(Verification, PatchTestReproducesAHarmonicTrilinearFieldExactly) {
    // T = 1 + 200x + 300y − 100z + 5e6·xyz satisfies ΔT = 0 and lies in the Q1 space, so the
    // FE solution must equal it at every node, on a graded grid (two boxes force uneven lines).
    const auto exact = [](double x, double y, double z) {
        return 1.0 + 200.0 * x + 300.0 * y - 100.0 * z + 5e6 * x * y * z;
    };
    Design d = boxDesign(0.010, 0.010, 0.010, 5.0, FaceBc::dirichlet(0.0));
    Box extra = d.boxes[0];
    extra.name = "extra";
    extra.min = {0.010, 0.0, 0.0};
    extra.max = {0.0133, 0.010, 0.010};
    d.boxes.push_back(extra);
    AssemblyOptions o;
    o.dirichlet = exact;
    const Solved s = solve(d, 2e-3, o);
    const ErrorNorms e = errorNorms(s.mesh, s.T, exact);
    EXPECT_LT(e.maxNodal, 1e-9);
    EXPECT_LT(e.l2 / std::sqrt(e.volume), 1e-9);
}

TEST(Verification, PatchTestWithConvectionOnEveryFace) {
    // Same field, convection on all faces with T∞ = T + (k/h)·∂T/∂n: still exact, because the
    // 2×2 Gauss rule integrates the face loads exactly.
    const double L = 0.010, k = 5.0, h = 40.0;
    const auto exact = [](double x, double y, double z) {
        return 1.0 + 200.0 * x + 300.0 * y - 100.0 * z + 5e6 * x * y * z;
    };
    const auto ambient = [&](double x, double y, double z) {
        const double gx = 200.0 + 5e6 * y * z;
        const double gy = 300.0 + 5e6 * x * z;
        const double gz = -100.0 + 5e6 * x * y;
        double dn = 0.0; // outward normal derivative, from the face the point lies on
        if (x <= 1e-12) {
            dn = -gx;
        } else if (x >= L - 1e-12) {
            dn = gx;
        } else if (y <= 1e-12) {
            dn = -gy;
        } else if (y >= L - 1e-12) {
            dn = gy;
        } else if (z <= 1e-12) {
            dn = -gz;
        } else {
            dn = gz;
        }
        return exact(x, y, z) + (k / h) * dn;
    };
    const Design d = boxDesign(L, L, L, k, FaceBc::convection(h, 0.0));
    AssemblyOptions o;
    o.ambient = ambient;
    const Solved s = solve(d, 2.5e-3, o);
    EXPECT_LT(errorNorms(s.mesh, s.T, exact).maxNodal, 1e-8);
}

// ---- The v1 bar in 3D -------------------------------------------------------------------------

TEST(Verification, V1BarIn3dReproducesTheV1Result) {
    // The 3D solution of the insulated bar does not depend on y and z and equals the 1D FE
    // solution, which is exact at the nodes: T = 100 + 420x − 500x², peak 188.2 °C at 0.42 m.
    const auto exact = [](double x, double, double) { return 100.0 + 420.0 * x - 500.0 * x * x; };
    for (const double s : {0.01, 0.005}) {
        const Solved r = solve(makeV1Bar(), s);
        const ErrorNorms e = errorNorms(r.mesh, r.T, exact);
        EXPECT_LT(e.maxNodal, 1e-7) << "s = " << s;
        const Peak p = peakTemperature(r.mesh, r.T);
        EXPECT_NEAR(p.t, 188.2, 1e-7) << "s = " << s;
        EXPECT_NEAR(p.at[0], 0.42, 1e-12) << "s = " << s;

        // Heat through each end (v1: 0.42 W left, 0.58 W right) and the global balance.
        const Vector reac = reactions(r.system, r.T);
        double left = 0.0, right = 0.0;
        for (std::size_t i = 0; i < reac.size(); ++i) {
            const double x =
                r.mesh.nodeXyz[static_cast<std::size_t>(r.system.dirichletNodes[i])][0];
            (x == 0.0 ? left : right) -= reac[i];
        }
        EXPECT_NEAR(left, 0.42, 1e-7) << "s = " << s;
        EXPECT_NEAR(right, 0.58, 1e-7) << "s = " << s;
        const EnergyAudit a = energyAudit(makeV1Bar(), r.mesh, r.system, r.T);
        EXPECT_NEAR(a.heatIn, 1.0, 1e-12);
        EXPECT_LT(a.relImbalance, 1e-8) << "s = " << s;
        std::printf(
            "[bar s=%.3f] DOF %zu, PCG-J %d it, max nodal error %.2e, ends %.10f / %.10f W\n", s,
            r.T.size(), r.report.iterations, e.maxNodal, left, right);
    }
}

// ---- Manufactured solution -------------------------------------------------------------------

TEST(Verification, ManufacturedSolutionConvergesAtSecondOrder) {
    // T = 300 + 20·cos(ax)·cos(by)·cos(cz) on a 20 mm cube, k = 200, convection on every face
    // (h = 1000) with T∞ = T + (k/h)·∂T/∂n, source q = k(a² + b² + c²)(T − 300).
    const double L = 0.020, k = 200.0, h = 1000.0;
    const double pi = std::acos(-1.0);
    const double a = 0.7 * pi / L, b = 0.5 * pi / L, c = 0.3 * pi / L;
    const auto exact = [=](double x, double y, double z) {
        return 300.0 + 20.0 * std::cos(a * x) * std::cos(b * y) * std::cos(c * z);
    };
    const auto source = [=](double x, double y, double z) {
        return k * (a * a + b * b + c * c) * (exact(x, y, z) - 300.0);
    };
    const auto ambient = [=](double x, double y, double z) {
        const double gx = -20.0 * a * std::sin(a * x) * std::cos(b * y) * std::cos(c * z);
        const double gy = -20.0 * b * std::cos(a * x) * std::sin(b * y) * std::cos(c * z);
        const double gz = -20.0 * c * std::cos(a * x) * std::cos(b * y) * std::sin(c * z);
        double dn = 0.0;
        if (x <= 1e-12) {
            dn = -gx;
        } else if (x >= L - 1e-12) {
            dn = gx;
        } else if (y <= 1e-12) {
            dn = -gy;
        } else if (y >= L - 1e-12) {
            dn = gy;
        } else if (z <= 1e-12) {
            dn = -gz;
        } else {
            dn = gz;
        }
        return exact(x, y, z) + (k / h) * dn;
    };
    const Design d = boxDesign(L, L, L, k, FaceBc::convection(h, 0.0));
    AssemblyOptions o;
    o.source = source;
    o.ambient = ambient;

    double previousL2 = 0.0;
    double previousNodal = 0.0;
    for (const int cells : {4, 8, 16, 32}) {
        const Solved r = solve(d, L / cells, o);
        const ErrorNorms e = errorNorms(r.mesh, r.T, exact);
        const EnergyAudit audit = energyAudit(d, r.mesh, r.system, r.T, o);
        EXPECT_LT(audit.relImbalance, 1e-8) << cells << " cells";
        EXPECT_NEAR(r.report.residualSum, audit.imbalance, 1e-9) << "1ᵀr is the heat imbalance";
        const double rms = e.l2 / std::sqrt(e.volume);
        if (previousL2 > 0.0) {
            const double orderL2 = std::log2(previousL2 / e.l2);
            const double orderNodal = std::log2(previousNodal / e.maxNodal);
            std::printf("[mms %2d cells] RMS error %.3e K, max nodal %.3e K, order L2 %.3f, "
                        "nodal %.3f, PCG-J %d it\n",
                        cells, rms, e.maxNodal, orderL2, orderNodal, r.report.iterations);
            if (cells >= 16) {
                EXPECT_NEAR(orderL2, 2.0, 0.1) << cells << " cells";
                EXPECT_NEAR(orderNodal, 2.0, 0.2) << cells << " cells";
            }
        } else {
            std::printf("[mms %2d cells] RMS error %.3e K, max nodal %.3e K, PCG-J %d it\n", cells,
                        rms, e.maxNodal, r.report.iterations);
        }
        previousL2 = e.l2;
        previousNodal = e.maxNodal;
    }
}

// ---- Fin theory -------------------------------------------------------------------------------

TEST(Verification, SingleFinMatchesOneDimensionalFinTheory) {
    // One HS-7 fin (2 × 40 × 26 mm, aluminium k = 200) on a base held at 80 °C, convection
    // h = 25 W/(m²·K) to 25 °C on every other face. Biot number h·t/(2k) ≈ 1.3e-4, so the 1D
    // fin equation with a convective tip applies [VERIFY: Incropera, fin with convective tip]:
    //   q_f = √(hPkA)·θb·(sinh mL + (h/mk)·cosh mL)/(cosh mL + (h/mk)·sinh mL)
    //   θ(L)/θb = 1/(cosh mL + (h/mk)·sinh mL),   m = √(hP/(kA)),  P = 2(W + t),  A = W·t
    const double t = 0.002, W = 0.040, Lf = 0.026, k = 200.0, h = 25.0, tInf = 25.0, tBase = 80.0;
    Design d = boxDesign(t, W, Lf, k, FaceBc::convection(h, tInf));
    d.boxes[0].faces[ZMin] = FaceBc::dirichlet(tBase);
    const Solved r = solve(d, 1e-3);

    const double P = 2.0 * (W + t), A = W * t, thetaB = tBase - tInf;
    const double m = std::sqrt(h * P / (k * A));
    const double ratio = h / (m * k);
    const double denom = std::cosh(m * Lf) + ratio * std::sinh(m * Lf);
    const double qTheory =
        std::sqrt(h * P * k * A) * thetaB * (std::sinh(m * Lf) + ratio * std::cosh(m * Lf)) / denom;
    const double tipTheory = tInf + thetaB / denom;

    const EnergyAudit a = energyAudit(d, r.mesh, r.system, r.T);
    auto tip = sampleTemperature(r.mesh, r.T, t / 2.0, W / 2.0, Lf);
    ASSERT_TRUE(tip.ok());
    std::printf("[fin] q FE %.5f W vs theory %.5f W (%.3f %%); tip FE %.4f vs theory %.4f °C\n",
                a.heatConvected, qTheory, 100.0 * (a.heatConvected / qTheory - 1.0), tip.value(),
                tipTheory);
    EXPECT_NEAR(a.heatConvected / qTheory, 1.0, 0.002); // measured −0.005 %
    EXPECT_NEAR(tip.value(), tipTheory, 0.1);           // measured 0.023 K
    EXPECT_LT(a.relImbalance, 1e-8);
    EXPECT_NEAR(a.heatOutDirichlet, -a.heatConvected, 1e-6) << "heat enters through the base";
}

// ---- HS-7 energy balance ----------------------------------------------------------------------

TEST(Verification, HeatSinkEnergyBalanceAndPlausibleTemperatures) {
    // Convection-only closure (near-singular): the energy audit checks the bookkeeping and the
    // solve; no closed-form solution exists.
    const Design hs = makeHS7();
    const Solved r = solve(hs, 2e-3, {}, 1e-12);
    const EnergyAudit a = energyAudit(hs, r.mesh, r.system, r.T);
    const Peak p = peakTemperature(r.mesh, r.T);
    std::printf("[HS-7 s=2mm] DOF %zu, PCG-J %d it (κ_est %.3e), T_max %.3f °C at z = %.5f m, "
                "heat %.6f W in, %.6f W convected, rel. imbalance %.2e\n",
                r.T.size(), r.report.iterations, r.report.kappaEstimate, p.t, p.at[2], a.heatIn,
                a.heatConvected, a.relImbalance);
    EXPECT_NEAR(a.heatIn, 30.0, 1e-9);
    EXPECT_LT(a.relImbalance, 1e-8);
    EXPECT_NEAR(r.report.residualSum, a.imbalance, 1e-7);
    EXPECT_GT(p.t, 25.0);
    EXPECT_LT(p.at[2], 0.0) << "the hottest node is in the die, below the base";
    // Every node is above ambient (heat only flows out).
    for (const double v : r.T) {
        ASSERT_GT(v, 25.0);
    }
}
