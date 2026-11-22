// IC(0) preconditioner (build spec §5.3, amendment A7): exactness without fill, fewer PCG
// iterations than Jacobi, breakdown on a non-M-matrix and recovery by the shift ladder.

#include "core/solvers/krylov/Ic0.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ModelProblems.hpp"
#include "core/common/Rng.hpp"
#include "core/solvers/direct/Cholesky.hpp"
#include "core/solvers/krylov/Pcg.hpp"

using namespace uniforge::solvers;
using uniforge::sparse::CsrMatrix;
using uniforge::sparse::KernelPool;
using uniforge::sparse::Offset;
using uniforge::testing::laplacian1d;
using uniforge::testing::laplacian2d;
using uniforge::testing::laplacian3d;

namespace {

Vector randomRhs(std::size_t n, std::uint64_t seed) {
    uniforge::Rng rng(seed);
    Vector f(n);
    for (double& v : f) {
        v = rng.uniform(-1.0, 1.0);
    }
    return f;
}

SolveReport pcgSolve(std::unique_ptr<Preconditioner> m, const CsrMatrix& K, const Vector& f,
                     KernelPool& pool, SetupReport* setupOut = nullptr) {
    Pcg pcg(SolverKind::PcgIc0, std::move(m));
    auto su = pcg.setup(K, {}, pool, {});
    EXPECT_TRUE(su.ok());
    if (setupOut != nullptr) {
        *setupOut = su.value();
    }
    Vector x(f.size(), 0.0);
    SolveControl c;
    c.relTol = 1e-10;
    auto r = pcg.solve(f, x, c);
    EXPECT_TRUE(r.ok());
    return r.value();
}

// SPD but not an M-matrix; IC(0) drops the fill entries (0, 2) and (1, 3) and the last pivot
// becomes 3 − 4/3 − 4/0.6 = −5 < 0 (exact Cholesky gives a positive 1/3). After Kershaw's
// counter-example [VERIFY: Kershaw 1978 before citing].
CsrMatrix kershaw() {
    return uniforge::sparse::fromTriplets(4, 4,
                                          {{0, 0, 3},
                                           {0, 1, -2},
                                           {0, 3, 2},
                                           {1, 0, -2},
                                           {1, 1, 3},
                                           {1, 2, -2},
                                           {2, 1, -2},
                                           {2, 2, 3},
                                           {2, 3, -2},
                                           {3, 0, 2},
                                           {3, 2, -2},
                                           {3, 3, 3}})
        .value();
}

} // namespace

TEST(Ic0, IsTheExactCholeskyFactorWhenThereIsNoFill) {
    // A tridiagonal matrix has no fill, so IC(0) = Cholesky and PCG converges in one step.
    KernelPool pool(1);
    const CsrMatrix K = laplacian1d(100);
    SetupReport su;
    const SolveReport r =
        pcgSolve(std::make_unique<Ic0Preconditioner>(), K, randomRhs(100, 1), pool, &su);
    EXPECT_EQ(r.status, SolveStatus::Converged);
    EXPECT_EQ(r.iterations, 1);
    EXPECT_EQ(su.preconditionerShift, 0.0);
    EXPECT_EQ(su.preconditionerRetries, 0);
}

TEST(Ic0, NeedsFewerIterationsThanJacobi) {
    KernelPool pool(1);
    for (const CsrMatrix& K : {laplacian2d(40), laplacian3d(16)}) {
        const Vector f = randomRhs(static_cast<std::size_t>(K.rows), 2);
        const SolveReport jac = pcgSolve(std::make_unique<JacobiPreconditioner>(), K, f, pool);
        const SolveReport ic0 = pcgSolve(std::make_unique<Ic0Preconditioner>(), K, f, pool);
        ASSERT_EQ(ic0.status, SolveStatus::Converged);
        std::printf("[ic0 n=%d] iterations: Jacobi %d, IC(0) %d; κ_est %.1f vs %.1f\n", K.rows,
                    jac.iterations, ic0.iterations, jac.kappaEstimate, ic0.kappaEstimate);
        EXPECT_LT(ic0.iterations * 3, jac.iterations * 2) << "at least a third fewer";
        EXPECT_LT(ic0.kappaEstimate, jac.kappaEstimate);
    }
}

TEST(Ic0, BreaksDownOnKershawsMatrixAndTheShiftLadderRecovers) {
    KernelPool pool(1);
    const CsrMatrix K = kershaw();
    // The matrix itself is SPD: the exact factorisation succeeds.
    DirectCholesky exact(SolverKind::DirectRCM, Ordering::Natural);
    ASSERT_EQ(exact.setup(K, {}, pool, {}).value().status, SolveStatus::Converged);

    Ic0Preconditioner noShift({0.0});
    const SetupReport fail = noShift.setup(K, pool);
    EXPECT_EQ(fail.status, SolveStatus::Breakdown);
    EXPECT_NE(fail.detail.find("row 3"), std::string::npos) << fail.detail;

    SetupReport su;
    const Vector f{1.0, 2.0, 3.0, 4.0};
    const SolveReport r = pcgSolve(std::make_unique<Ic0Preconditioner>(), K, f, pool, &su);
    EXPECT_EQ(su.status, SolveStatus::Converged);
    EXPECT_GT(su.preconditionerShift, 0.0);
    EXPECT_GE(su.preconditionerRetries, 1);
    EXPECT_FALSE(su.detail.empty());
    EXPECT_EQ(r.status, SolveStatus::Converged);
    EXPECT_LT(r.finalTrueRelResidual, 1e-9);
}

TEST(Ic0, ExhaustedLadderIsABreakdownOfTheSetup) {
    KernelPool pool(1);
    Pcg pcg(SolverKind::PcgIc0,
            std::make_unique<Ic0Preconditioner>(std::vector<double>{0.0, 1e-6}));
    auto su = pcg.setup(kershaw(), {}, pool, {});
    ASSERT_TRUE(su.ok());
    EXPECT_EQ(su.value().status, SolveStatus::Breakdown);
    EXPECT_EQ(su.value().preconditionerRetries, 2);
    Vector x(4, 0.0);
    EXPECT_FALSE(pcg.solve({1, 1, 1, 1}, x, {}).ok()) << "no successful setup";
}

TEST(Ic0, FactorKeepsThePatternOfTheLowerTriangle) {
    KernelPool pool(1);
    const CsrMatrix K = laplacian2d(10);
    Ic0Preconditioner m;
    ASSERT_EQ(m.setup(K, pool).status, SolveStatus::Converged);
    const CsrMatrix& L = m.factor();
    Offset lowerEntries = 0;
    for (Index i = 0; i < K.rows; ++i) {
        for (Offset q = K.rowPtr[uniforge::sparse::toSize(i)];
             q < K.rowPtr[uniforge::sparse::toSize(i) + 1]; ++q) {
            if (K.col[uniforge::sparse::toSize(q)] <= i) {
                ++lowerEntries;
                EXPECT_GE(L.find(i, K.col[uniforge::sparse::toSize(q)]), 0);
            }
        }
    }
    EXPECT_EQ(L.nnz(), lowerEntries) << "zero fill";
}
