// Smoothed-aggregation AMG (build spec §5.4, V2-7 exit criteria): aggregation, P preserves
// constants, Galerkin operator, symmetric V-cycle (amendment A15), convergence independent of
// the mesh size, AMG-PCG.

#include "core/solvers/amg/Amg.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "ModelProblems.hpp"
#include "core/common/Rng.hpp"
#include "core/solvers/krylov/Ic0.hpp"
#include "core/solvers/krylov/Pcg.hpp"
#include "core/sparse/SparseOps.hpp"

using namespace uniforge::solvers;
using uniforge::ErrorCode;
using uniforge::sparse::CsrMatrix;
using uniforge::sparse::KernelPool;
using uniforge::sparse::Offset;
using uniforge::sparse::toSize;
using uniforge::testing::laplacian2d;
using uniforge::testing::laplacian3d;
using uniforge::testing::q1Laplacian3d;

namespace {

Vector randomVec(std::size_t n, std::uint64_t seed) {
    uniforge::Rng rng(seed);
    Vector f(n);
    for (double& v : f) {
        v = rng.uniform(-1.0, 1.0);
    }
    return f;
}

double dotv(const Vector& a, const Vector& b) {
    double s = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        s += a[i] * b[i];
    }
    return s;
}

// Graph Laplacian of a 2D grid with zero row sums (pure Neumann): constants are its null space.
CsrMatrix neumann2d(Index m) {
    CsrMatrix A = laplacian2d(m);
    for (Index r = 0; r < A.rows; ++r) {
        double off = 0.0;
        Offset d = -1;
        for (Offset q = A.rowPtr[toSize(r)]; q < A.rowPtr[toSize(r) + 1]; ++q) {
            if (A.col[toSize(q)] == r) {
                d = q;
            } else {
                off += A.val[toSize(q)];
            }
        }
        A.val[toSize(d)] = -off;
    }
    return A;
}

SolverOptions smallCoarse() {
    SolverOptions o;
    o.amgCoarseSize = 50;
    return o;
}

} // namespace

TEST(Amg, AggregatesCoverEveryConnectedNodeAndLeaveIsolatedOnesOut) {
    CsrMatrix A = laplacian2d(12);
    // Make node 0 isolated (an identity row, like a Dirichlet node).
    for (Offset q = A.rowPtr[0]; q < A.rowPtr[1]; ++q) {
        A.val[toSize(q)] = A.col[toSize(q)] == 0 ? 1.0 : 0.0;
    }
    const auto strong = strongConnections(A, 0.08);
    Index count = 0;
    const auto agg = aggregate(A, strong, count);
    EXPECT_EQ(agg[0], -1);
    std::set<Index> used;
    for (std::size_t i = 1; i < agg.size(); ++i) {
        ASSERT_GE(agg[i], 0) << "node " << i;
        ASSERT_LT(agg[i], count);
        used.insert(agg[i]);
    }
    EXPECT_EQ(static_cast<Index>(used.size()), count) << "no empty aggregates";
    EXPECT_LT(count * 4, A.rows) << "aggregates of about 9 nodes on a 5-point grid";
}

TEST(Amg, ProlongatorPreservesConstants) {
    // For a matrix with zero row sums, A_F·1 = 0, so P·1 = P0·1 = 1 on every aggregated node.
    const CsrMatrix A = neumann2d(15);
    const auto strong = strongConnections(A, 0.08);
    Index count = 0;
    const auto agg = aggregate(A, strong, count);
    const CsrMatrix P0 = tentativeProlongator(agg, count);
    const CsrMatrix P = smoothedProlongator(A, strong, P0);
    ASSERT_TRUE(uniforge::sparse::validate(P).ok());
    Vector ones(toSize(count), 1.0);
    Vector y(toSize(A.rows), 0.0);
    KernelPool pool(1);
    uniforge::sparse::spmv(pool, P, ones, y);
    for (std::size_t i = 0; i < y.size(); ++i) {
        ASSERT_NEAR(y[i], 1.0, 1e-13) << "row " << i;
    }
}

TEST(Amg, CoarseOperatorsAreGalerkinSymmetricAndPositiveDefinite) {
    KernelPool pool(1);
    const CsrMatrix A = laplacian3d(12);
    AmgHierarchy h;
    auto r = h.build(A, pool, smallCoarse());
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.value().status, SolveStatus::Converged) << r.value().detail;
    ASSERT_GE(h.levelCount(), 3U);
    for (std::size_t l = 1; l < h.levelCount(); ++l) {
        EXPECT_TRUE(isSymmetric(h.op(l), 1e-12)) << "level " << l;
        EXPECT_LT(h.op(l).rows, h.op(l - 1).rows);
    }
    // Every coarse operator factorises (SPD): the coarsest one already did inside build().
    DirectCholesky check(SolverKind::DirectRCM, Ordering::Rcm);
    EXPECT_EQ(check.setup(h.op(1), {}, pool, {}).value().status, SolveStatus::Converged);
    EXPECT_GT(r.value().operatorComplexity, 1.0);
    EXPECT_LT(r.value().operatorComplexity, 3.0);
    EXPECT_EQ(r.value().levels.size(), h.levelCount());
}

TEST(Amg, VcyclePreconditionerIsSymmetricPositiveDefinite) {
    // Amendment A15: forward pre-smoothing, backward post-smoothing, exact coarse solve.
    KernelPool pool(1);
    const CsrMatrix A = laplacian3d(10);
    AmgPreconditioner M(smallCoarse());
    ASSERT_EQ(M.setup(A, pool).status, SolveStatus::Converged);
    const std::size_t n = toSize(A.rows);
    for (std::uint64_t seed = 1; seed <= 3; ++seed) {
        const Vector x = randomVec(n, seed);
        const Vector y = randomVec(n, seed + 100);
        Vector mx(n), my(n);
        M.apply(x, mx);
        M.apply(y, my);
        const double a = dotv(y, mx);
        const double b = dotv(x, my);
        EXPECT_NEAR(a, b, 1e-10 * std::fabs(a)) << "yᵀM⁻¹x = xᵀM⁻¹y";
        EXPECT_GT(dotv(x, mx), 0.0);
    }
}

TEST(Amg, ConvergenceDoesNotDegradeWithMeshSize) {
    // 7-point stencil at the θ₀ = 0.08 it was tuned on in V2-7 (all its couplings are 1/6 of the
    // diagonal). The default θ₀ = 0.02 is checked on Q1 hexahedra below.
    KernelPool pool(1);
    std::vector<int> amgIts;
    SolverOptions o = smallCoarse();
    o.amgStrengthThreshold = 0.08;
    for (const Index m : {10, 16, 24}) {
        const CsrMatrix A = laplacian3d(m);
        const Vector f = randomVec(toSize(A.rows), 7);
        AmgSolver amg;
        auto su = amg.setup(A, {}, pool, o);
        ASSERT_TRUE(su.ok());
        ASSERT_EQ(su.value().status, SolveStatus::Converged);
        Vector x(f.size(), 0.0);
        SolveControl c;
        c.relTol = 1e-8;
        auto r = amg.solve(f, x, c);
        ASSERT_TRUE(r.ok());
        ASSERT_EQ(r.value().status, SolveStatus::Converged) << r.value().detail;
        EXPECT_LT(r.value().finalTrueRelResidual, 1e-8);
        const auto& h = r.value().residualHistory;
        const double factor =
            std::pow(h.back() / h.front(), 1.0 / static_cast<double>(h.size() - 1));
        std::printf("[amg %d³] levels %zu, op. complexity %.2f, %d cycles, mean factor %.3f\n", m,
                    su.value().levels.size(), su.value().operatorComplexity, r.value().iterations,
                    factor);
        EXPECT_LT(factor, 0.5);
        amgIts.push_back(r.value().iterations);
    }
    EXPECT_LE(amgIts.back(), amgIts.front() + 4) << "iterations roughly independent of size";
}

// The V2-9 finding: on trilinear hexahedra of aspect ratio 1 every coupling is at most 1/16 of
// the diagonal, so θ₀ = 0.08 marks all of them weak and no node is aggregated: on this uniform
// grid the hierarchy has no coarse level at all (on the HS-7 meshes, partly, which left many
// nodes with smoothing only and AMG stagnated). The default θ₀ = 0.02 keeps them strong and
// converges in a size-independent number of cycles.
TEST(Amg, DefaultThresholdHandlesTrilinearHexahedra) {
    KernelPool pool(1);
    EXPECT_DOUBLE_EQ(SolverOptions{}.amgStrengthThreshold, 0.02);
    std::vector<int> its;
    for (const Index m : {10, 16, 24}) {
        const CsrMatrix A = q1Laplacian3d(m);
        const Vector f = randomVec(toSize(A.rows), 11);
        AmgSolver amg;
        auto su = amg.setup(A, {}, pool, smallCoarse());
        ASSERT_TRUE(su.ok());
        EXPECT_GE(su.value().levels.size(), 3u);
        Vector x(f.size(), 0.0);
        SolveControl c;
        c.relTol = 1e-8;
        c.maxIterations = 200;
        auto r = amg.solve(f, x, c);
        ASSERT_TRUE(r.ok());
        ASSERT_EQ(r.value().status, SolveStatus::Converged) << r.value().detail;
        std::printf("[amg q1 %d³] levels %zu, %d cycles\n", m, su.value().levels.size(),
                    r.value().iterations);
        its.push_back(r.value().iterations);
    }
    EXPECT_LE(its.back(), 30);
    EXPECT_LE(its.back(), its.front() + 6) << "cycles roughly independent of size";

    // The old threshold: every coupling is weak, so no node is aggregated.
    const CsrMatrix A = q1Laplacian3d(16);
    Index count = 0;
    const auto old = aggregate(A, strongConnections(A, 0.08), count);
    EXPECT_EQ(count, 0);
    EXPECT_TRUE(std::all_of(old.begin(), old.end(), [](Index g) { return g == -1; }));
    const auto now = aggregate(A, strongConnections(A, 0.02), count);
    EXPECT_GT(count, 0);
    EXPECT_TRUE(std::none_of(now.begin(), now.end(), [](Index g) { return g == -1; }));
}

TEST(Amg, AmgPcgBeatsIc0AndJacobiOnALarger3dGrid) {
    KernelPool pool(1);
    const CsrMatrix A = laplacian3d(24);
    const Vector f = randomVec(toSize(A.rows), 9);
    SolveControl c;
    c.relTol = 1e-10;
    const auto run = [&](std::unique_ptr<Preconditioner> m) {
        Pcg pcg(SolverKind::AmgPcg, std::move(m));
        EXPECT_EQ(pcg.setup(A, {}, pool, {}).value().status, SolveStatus::Converged);
        Vector x(f.size(), 0.0);
        auto r = pcg.solve(f, x, c);
        EXPECT_TRUE(r.ok());
        EXPECT_EQ(r.value().status, SolveStatus::Converged);
        return r.value().iterations;
    };
    const int jac = run(std::make_unique<JacobiPreconditioner>());
    const int ic0 = run(std::make_unique<Ic0Preconditioner>());
    const int amg = run(std::make_unique<AmgPreconditioner>(smallCoarse()));
    std::printf("[24³ PCG iterations] Jacobi %d, IC(0) %d, AMG %d\n", jac, ic0, amg);
    EXPECT_LT(amg, ic0);
    EXPECT_LT(ic0, jac);
}

// V3a: a single-precision hierarchy under double CG reaches the same 1e-10 tolerance with
// nearly the same iterations, the same answer, and fewer bytes per V-cycle.
TEST(Amg, SinglePrecisionHierarchyKeepsCgAccuracy) {
    KernelPool pool(2);
    const CsrMatrix A = q1Laplacian3d(20);
    const Vector f = randomVec(toSize(A.rows), 21);
    SolveControl c;
    c.relTol = 1e-10;
    struct Out {
        int iterations;
        double trueResidual;
        double bytesPerCycle;
        Vector x;
    };
    const auto run = [&](bool single) {
        SolverOptions o = smallCoarse();
        o.amgSinglePrecision = single;
        auto m = std::make_unique<AmgPreconditioner>(o);
        const AmgPreconditioner* raw = m.get();
        Pcg pcg(single ? SolverKind::AmgPcgSp : SolverKind::AmgPcg, std::move(m));
        EXPECT_EQ(pcg.setup(A, {}, pool, {}).value().status, SolveStatus::Converged);
        EXPECT_EQ(raw->hierarchy().singlePrecision(), single);
        EXPECT_GE(raw->hierarchy().levelCount(), 3u);
        Vector x(f.size(), 0.0);
        auto r = pcg.solve(f, x, c);
        EXPECT_TRUE(r.ok());
        EXPECT_EQ(r.value().status, SolveStatus::Converged);
        return Out{r.value().iterations, r.value().finalTrueRelResidual,
                   raw->hierarchy().bytesPerCycle(), x};
    };
    const Out d = run(false);
    const Out s = run(true);
    std::printf("[20³ Q1 AMG-PCG] double %d its, single %d its; bytes/cycle %.3g vs %.3g\n",
                d.iterations, s.iterations, d.bytesPerCycle, s.bytesPerCycle);
    EXPECT_LE(s.iterations, d.iterations + 2);
    EXPECT_LT(s.trueResidual, 1e-9);
    EXPECT_LT(s.bytesPerCycle, 0.8 * d.bytesPerCycle);
    double diff = 0.0;
    double scale = 0.0;
    for (std::size_t i = 0; i < d.x.size(); ++i) {
        diff = std::max(diff, std::fabs(d.x[i] - s.x[i]));
        scale = std::max(scale, std::fabs(d.x[i]));
    }
    EXPECT_LT(diff, 1e-7 * scale);
}

// V3b: the Chebyshev–Jacobi smoother. Its λ̂ bounds λ_max(D⁻¹A) from above, AMG-PCG with it
// converges in a mesh-independent number of iterations, the V-cycle stays symmetric, and the
// result is bit-identical for 1 and 4 threads (chunked kernels, amendment N2).
TEST(Amg, ChebyshevSmootherConvergesAndIsThreadIndependent) {
    const CsrMatrix A = q1Laplacian3d(16);
    SolverOptions o = smallCoarse();
    o.amgSmoother = AmgSmoother::Chebyshev;
    {
        KernelPool pool(1);
        AmgHierarchy h;
        ASSERT_TRUE(h.build(A, pool, o).ok());
        EXPECT_EQ(h.smoother(), AmgSmoother::Chebyshev);
        // Gershgorin: λ_max(D⁻¹A) ≤ max_i Σ_j |a_ij| / a_ii. λ̂ must be positive, at most 1.1×
        // that bound, and close to it from below (power iteration plus the 10 % margin).
        double gersh = 0.0;
        for (Index i = 0; i < A.rows; ++i) {
            double s = 0.0;
            for (Offset q = A.rowPtr[toSize(i)]; q < A.rowPtr[toSize(i) + 1]; ++q) {
                s += std::fabs(A.val[toSize(q)]);
            }
            gersh = std::max(gersh, s / A.at(i, i));
        }
        EXPECT_GT(h.chebyshevLambdaMax(0), 0.5 * gersh);
        EXPECT_LE(h.chebyshevLambdaMax(0), 1.1 * gersh + 1e-12);
        // Symmetry of the V-cycle as a linear operator: uᵀM⁻¹v = vᵀM⁻¹u.
        const Vector u = randomVec(toSize(A.rows), 31);
        const Vector v = randomVec(toSize(A.rows), 32);
        Vector Mu(u.size(), 0.0);
        Vector Mv(v.size(), 0.0);
        h.vcycle(u, Mu);
        h.vcycle(v, Mv);
        EXPECT_NEAR(dotv(v, Mu), dotv(u, Mv), 1e-10 * std::fabs(dotv(v, Mu)));
    }
    const auto solve = [&](const CsrMatrix& M, int threads, bool single) {
        KernelPool pool(threads);
        SolverOptions oo = o;
        oo.amgSinglePrecision = single;
        Pcg pcg(SolverKind::AmgPcgCheb, std::make_unique<AmgPreconditioner>(oo));
        EXPECT_EQ(pcg.setup(M, {}, pool, {}).value().status, SolveStatus::Converged);
        Vector x(toSize(M.rows), 0.0);
        SolveControl c;
        c.relTol = 1e-10;
        auto r = pcg.solve(randomVec(x.size(), 7), x, c);
        EXPECT_TRUE(r.ok());
        EXPECT_EQ(r.value().status, SolveStatus::Converged);
        return std::make_pair(r.value().iterations, x);
    };
    const auto [it1, x1] = solve(A, 1, false);
    const auto [it4, x4] = solve(A, 4, false);
    EXPECT_EQ(it1, it4);
    EXPECT_EQ(x1, x4);
    const auto [itSp, xSp] = solve(A, 4, true);
    EXPECT_LE(itSp, it1 + 2);
    const auto [itBig, xBig] = solve(q1Laplacian3d(28), 4, false);
    std::printf("[Q1 AMG-PCG Chebyshev] 16³ %d its (single %d), 28³ %d its\n", it1, itSp, itBig);
    EXPECT_LE(itBig, it1 + 6);
}

// V3c: the setup runs on the pool. The hierarchy (aggregates, every coarse operator, λ̂) is
// bit-identical for 1, 4 and 8 threads, so solver results do not depend on the thread count.
TEST(Amg, ParallelSetupBuildsTheSameHierarchyForAnyThreadCount) {
    const CsrMatrix A = q1Laplacian3d(26); // 17,576 rows: several 4096-row chunks
    SolverOptions o = smallCoarse();
    o.amgSmoother = AmgSmoother::Chebyshev;
    KernelPool pool1(1);
    AmgHierarchy ref;
    ASSERT_TRUE(ref.build(A, pool1, o).ok());
    ASSERT_GE(ref.levelCount(), 3u);
    for (const int threads : {4, 8}) {
        KernelPool pool(threads);
        AmgHierarchy h;
        ASSERT_TRUE(h.build(A, pool, o).ok());
        ASSERT_EQ(h.levelCount(), ref.levelCount()) << threads;
        for (std::size_t l = 0; l < h.levelCount(); ++l) {
            EXPECT_EQ(h.op(l).col, ref.op(l).col) << threads << " level " << l;
            EXPECT_EQ(h.op(l).val, ref.op(l).val) << threads << " level " << l;
            if (l + 1 < h.levelCount()) {
                EXPECT_EQ(h.aggregates(l), ref.aggregates(l));
                EXPECT_EQ(h.chebyshevLambdaMax(l), ref.chebyshevLambdaMax(l));
            }
        }
    }
    // The parallel building blocks equal their serial calls.
    KernelPool pool(4);
    const auto strong = strongConnections(A, 0.02);
    EXPECT_EQ(strongConnections(A, 0.02, &pool), strong);
    Index count = 0;
    const std::vector<Index> agg = aggregate(A, strong, count); // sets count: own statement
    const CsrMatrix P0 = tentativeProlongator(agg, count);
    const CsrMatrix P = smoothedProlongator(A, strong, P0);
    const CsrMatrix Pp = smoothedProlongator(A, strong, P0, &pool);
    EXPECT_EQ(Pp.col, P.col);
    EXPECT_EQ(Pp.val, P.val);
}

// Stand-alone AMG ignores the flag: its iterate must stay in double precision.
TEST(Amg, StandAloneAmgStaysInDoublePrecision) {
    KernelPool pool(1);
    const CsrMatrix A = q1Laplacian3d(12);
    SolverOptions o = smallCoarse();
    o.amgSinglePrecision = true;
    AmgSolver amg;
    ASSERT_TRUE(amg.setup(A, {}, pool, o).ok());
    EXPECT_FALSE(amg.hierarchy().singlePrecision());
    Vector x(toSize(A.rows), 0.0);
    SolveControl c;
    c.relTol = 1e-10;
    auto r = amg.solve(randomVec(x.size(), 5), x, c);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().status, SolveStatus::Converged);
}

TEST(Amg, StatusesAndMisuse) {
    KernelPool pool(1);
    const CsrMatrix A = laplacian3d(10);
    AmgSolver amg;
    Vector x(toSize(A.rows), 0.0);
    EXPECT_EQ(amg.solve(Vector(x.size(), 1.0), x, {}).error().code, ErrorCode::InvalidInput);
    ASSERT_TRUE(amg.setup(A, {}, pool, smallCoarse()).ok());
    SolveControl c;
    c.maxIterations = 2;
    EXPECT_EQ(amg.solve(randomVec(x.size(), 3), x, c).value().status, SolveStatus::MaxIterations);
    SolverOptions tiny = smallCoarse();
    tiny.memoryBudgetBytes = 10;
    AmgSolver small;
    EXPECT_EQ(small.setup(A, {}, pool, tiny).value().status, SolveStatus::OverBudget);
    // A small matrix needs no coarsening: one level, solved directly.
    AmgSolver direct;
    auto su = direct.setup(laplacian2d(5), {}, pool, {});
    ASSERT_TRUE(su.ok());
    EXPECT_EQ(su.value().levels.size(), 1U);
}
