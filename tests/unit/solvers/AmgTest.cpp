// Smoothed-aggregation AMG (build spec §5.4, V2-7 exit criteria): aggregation, P preserves
// constants, Galerkin operator, symmetric V-cycle (amendment A15), convergence independent of
// the mesh size, AMG-PCG.

#include "core/solvers/amg/Amg.hpp"

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
    KernelPool pool(1);
    std::vector<int> amgIts;
    for (const Index m : {10, 16, 24}) {
        const CsrMatrix A = laplacian3d(m);
        const Vector f = randomVec(toSize(A.rows), 7);
        AmgSolver amg;
        auto su = amg.setup(A, {}, pool, smallCoarse());
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
