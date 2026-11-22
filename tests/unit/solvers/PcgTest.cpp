// Preconditioned conjugate gradients (build spec §5.3, V2-3 exit criteria): convergence to the
// dense solution, κ estimate, determinism for any thread count, observers, failure statuses.

#include "core/solvers/krylov/Pcg.hpp"

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "ModelProblems.hpp"
#include "core/common/Rng.hpp"
#include "core/solvers/Observers.hpp"
#include "support/DenseGauss.hpp"

using namespace uniforge::solvers;
using uniforge::ErrorCode;
using uniforge::sparse::CsrMatrix;
using uniforge::sparse::KernelPool;
using uniforge::testing::badlyScaled;
using uniforge::testing::laplacian1d;
using uniforge::testing::laplacian1dCondition;
using uniforge::testing::laplacian2d;

namespace {

Vector randomRhs(std::size_t n, std::uint64_t seed) {
    uniforge::Rng rng(seed);
    Vector f(n);
    for (double& v : f) {
        v = rng.uniform(-1.0, 1.0);
    }
    return f;
}

Vector denseReference(const CsrMatrix& K, const Vector& f) {
    const auto flat = toDense(K);
    const std::size_t n = f.size();
    uniforge::testing::DenseMatrix m(n, std::vector<double>(n));
    for (std::size_t r = 0; r < n; ++r) {
        for (std::size_t c = 0; c < n; ++c) {
            m[r][c] = flat[r * n + c];
        }
    }
    return uniforge::testing::denseSolve(m, f);
}

std::unique_ptr<Pcg> jacobiPcg() {
    return std::make_unique<Pcg>(SolverKind::PcgJacobi, std::make_unique<JacobiPreconditioner>());
}

std::unique_ptr<Pcg> plainCg() {
    return std::make_unique<Pcg>(SolverKind::PcgJacobi, std::make_unique<IdentityPreconditioner>());
}

struct Outcome {
    SetupReport setup;
    SolveReport solve;
    Vector x;
};

Outcome runSolver(LinearOperatorSolver& s, const CsrMatrix& K, const Vector& f, KernelPool& pool,
                  SolveControl control = {}, SolverOptions options = {}) {
    Outcome out;
    auto su = s.setup(K, {}, pool, options);
    EXPECT_TRUE(su.ok());
    out.setup = su.value();
    out.x.assign(f.size(), 0.0);
    if (out.setup.status != SolveStatus::Converged) {
        return out;
    }
    auto so = s.solve(f, out.x, control);
    EXPECT_TRUE(so.ok()) << (so.ok() ? "" : so.error().message);
    out.solve = so.value();
    return out;
}

} // namespace

TEST(Pcg, SolvesThe2dLaplacianToTheDenseSolution) {
    KernelPool pool(4);
    const CsrMatrix K = laplacian2d(20); // 400 unknowns
    const Vector f = randomRhs(400, 1);
    auto s = jacobiPcg();
    SolveControl c;
    c.relTol = 1e-12;
    const Outcome r = runSolver(*s, K, f, pool, c);
    ASSERT_EQ(r.solve.status, SolveStatus::Converged);
    EXPECT_LE(r.solve.finalTrueRelResidual, 1e-11);
    const Vector ref = denseReference(K, f);
    for (std::size_t i = 0; i < ref.size(); ++i) {
        ASSERT_NEAR(r.x[i], ref[i], 1e-9 * (1.0 + std::fabs(ref[i])));
    }
    EXPECT_EQ(static_cast<std::size_t>(r.solve.iterations) + 1, r.solve.residualHistory.size());
    EXPECT_EQ(static_cast<std::size_t>(r.solve.iterations), r.solve.iterTimestamps.size());
    EXPECT_EQ(r.solve.spmvCount, r.solve.iterations + 1);
    EXPECT_GT(r.solve.workSpmvEq, static_cast<double>(r.solve.spmvCount));
    EXPECT_GT(r.setup.footprint.total(), 0U);
}

TEST(Pcg, KappaEstimateMatchesTheKnownConditionNumber) {
    // Constant diagonal: Jacobi only scales, so κ(M⁻¹K) = κ(K) = known in closed form.
    KernelPool pool(1);
    const uniforge::sparse::Index n = 200;
    const CsrMatrix K = laplacian1d(n);
    auto s = jacobiPcg();
    SolveControl c;
    c.relTol = 1e-12;
    const Outcome r = runSolver(*s, K, randomRhs(200, 2), pool, c);
    ASSERT_EQ(r.solve.status, SolveStatus::Converged);
    const double exact = laplacian1dCondition(n);
    // Ritz values approach the extreme eigenvalues from inside: a lower bound, tight at
    // convergence.
    EXPECT_LE(r.solve.kappaEstimate, exact * (1.0 + 1e-6));
    EXPECT_GE(r.solve.kappaEstimate, 0.9 * exact);
}

TEST(Pcg, JacobiBeatsPlainCgOnABadlyScaledMatrix) {
    KernelPool pool(2);
    const CsrMatrix K = badlyScaled(laplacian2d(15));
    const Vector f = randomRhs(225, 3);
    SolveControl c;
    c.relTol = 1e-8;
    c.maxIterations = 5000;
    auto cg = plainCg();
    auto pcg = jacobiPcg();
    const Outcome plain = runSolver(*cg, K, f, pool, c);
    const Outcome jac = runSolver(*pcg, K, f, pool, c);
    ASSERT_EQ(jac.solve.status, SolveStatus::Converged);
    EXPECT_LT(jac.solve.iterations * 3, plain.solve.iterations)
        << "Jacobi " << jac.solve.iterations << " vs plain " << plain.solve.iterations;
    EXPECT_LT(jac.solve.kappaEstimate, plain.solve.kappaEstimate);
}

TEST(Pcg, BitIdenticalForEveryThreadCount) {
    const CsrMatrix K = laplacian2d(90); // 8100 rows: two kernel chunks
    const Vector f = randomRhs(8100, 4);
    SolveControl c;
    c.relTol = 1e-10;
    KernelPool p1(1);
    auto s1 = jacobiPcg();
    const Outcome ref = runSolver(*s1, K, f, p1, c);
    ASSERT_EQ(ref.solve.status, SolveStatus::Converged);
    for (const int p : {2, 3, 8}) {
        KernelPool pool(p);
        auto s = jacobiPcg();
        const Outcome r = runSolver(*s, K, f, pool, c);
        EXPECT_EQ(r.solve.iterations, ref.solve.iterations) << "p = " << p;
        EXPECT_EQ(r.solve.residualHistory, ref.solve.residualHistory) << "p = " << p;
        EXPECT_EQ(r.x, ref.x) << "p = " << p;
        EXPECT_EQ(r.solve.kappaEstimate, ref.solve.kappaEstimate) << "p = " << p;
    }
}

TEST(Pcg, ObserverTracksErrorAndItsTimeIsExcluded) {
    KernelPool pool(2);
    const CsrMatrix K = laplacian2d(20);
    const Vector f = randomRhs(400, 5);
    const Vector ref = denseReference(K, f);
    ErrorVsReference obs(ref, pool);
    SolveControl c;
    c.relTol = 1e-12;
    c.observer = &obs;
    auto s = jacobiPcg();
    const Outcome r = runSolver(*s, K, f, pool, c);
    ASSERT_EQ(r.solve.status, SolveStatus::Converged);
    ASSERT_EQ(obs.errors().size(), static_cast<std::size_t>(r.solve.iterations));
    EXPECT_LT(obs.errors().back(), 1e-8);
    EXPECT_GT(obs.errors().front(), obs.errors().back());
    const int first = obs.firstIterationBelow(1e-3);
    EXPECT_GT(first, 0);
    EXPECT_LE(first, r.solve.iterations);
    EXPECT_GE(r.solve.observerSeconds, 0.0);
    EXPECT_GE(r.solve.seconds, 0.0);

    Snapshot shots(10);
    c.observer = &shots;
    Vector x(400, 0.0);
    ASSERT_TRUE(s->solve(f, x, c).ok());
    ASSERT_FALSE(shots.shots().empty());
    EXPECT_EQ(shots.shots().front().iteration, 10);
}

TEST(Pcg, SetupOnceSolveManyLoadCases) {
    KernelPool pool(2);
    const CsrMatrix K = laplacian2d(12);
    auto s = jacobiPcg();
    ASSERT_TRUE(s->setup(K, {}, pool, {}).ok());
    for (std::uint64_t seed = 10; seed < 13; ++seed) {
        const Vector f = randomRhs(144, seed);
        Vector x(144, 0.0);
        auto r = s->solve(f, x, {});
        ASSERT_TRUE(r.ok());
        EXPECT_EQ(r.value().status, SolveStatus::Converged);
        EXPECT_LE(r.value().finalTrueRelResidual, 1e-9);
    }
}

TEST(Pcg, ResidualSumIsTheSumOfTheTrueResidualOverMaskedRows) {
    KernelPool pool(1);
    const CsrMatrix K = laplacian1d(50);
    const Vector f(50, 1.0);
    std::vector<std::uint8_t> mask(50, 1);
    mask[0] = 0;
    SolveControl c;
    c.maxIterations = 3; // stop early so the residual is not small
    c.residualSumMask = &mask;
    auto s = jacobiPcg();
    const Outcome r = runSolver(*s, K, f, pool, c);
    EXPECT_EQ(r.solve.status, SolveStatus::MaxIterations);
    Vector res(50);
    uniforge::sparse::residual(pool, K, r.x, f, res);
    double expected = 0.0;
    for (std::size_t i = 1; i < 50; ++i) {
        expected += res[i];
    }
    EXPECT_DOUBLE_EQ(r.solve.residualSum, expected);
}

TEST(Pcg, ZeroRightHandSideGivesZeroSolution) {
    KernelPool pool(1);
    const CsrMatrix K = laplacian1d(10);
    auto s = jacobiPcg();
    ASSERT_TRUE(s->setup(K, {}, pool, {}).ok());
    Vector x(10, 3.0);
    auto r = s->solve(Vector(10, 0.0), x, {});
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().status, SolveStatus::Converged);
    EXPECT_EQ(r.value().iterations, 0);
    EXPECT_EQ(x, Vector(10, 0.0));
}

TEST(Pcg, FailureStatuses) {
    KernelPool pool(1);
    const CsrMatrix K = laplacian2d(20);
    const Vector f = randomRhs(400, 6);

    SolveControl c;
    c.maxIterations = 5;
    auto s = jacobiPcg();
    EXPECT_EQ(runSolver(*s, K, f, pool, c).solve.status, SolveStatus::MaxIterations);

    c = {};
    c.wallTimeCap = 0.0;
    EXPECT_EQ(runSolver(*s, K, f, pool, c).solve.status, SolveStatus::TimeCap);

    c = {};
    uniforge::CancelToken cancel;
    cancel.requestCancel();
    c.cancel = &cancel;
    EXPECT_EQ(runSolver(*s, K, f, pool, c).solve.status, SolveStatus::Cancelled);

    c = {};
    c.stagnationWindow = 2; // the 2D Laplacian needs more than 2 iterations per 10× drop
    EXPECT_EQ(runSolver(*s, K, f, pool, c).solve.status, SolveStatus::Stagnated);

    SolverOptions tiny;
    tiny.memoryBudgetBytes = 1;
    EXPECT_EQ(runSolver(*s, K, f, pool, {}, tiny).setup.status, SolveStatus::OverBudget);
}

TEST(Pcg, IndefiniteMatrixIsABreakdownNotAWrongAnswer) {
    KernelPool pool(1);
    // diag(1, −1): rᵀKr can be ≤ 0. Plain CG sees pᵀKp ≤ 0 or rᵀz ≤ 0; Jacobi refuses at setup.
    const CsrMatrix K = uniforge::sparse::fromTriplets(2, 2, {{0, 0, 1.0}, {1, 1, -1.0}}).value();
    auto jac = jacobiPcg();
    EXPECT_EQ(runSolver(*jac, K, {1.0, 1.0}, pool).setup.status, SolveStatus::Breakdown);
    auto cg = plainCg();
    EXPECT_EQ(runSolver(*cg, K, {1.0, 1.0}, pool).solve.status, SolveStatus::Breakdown);
}

TEST(Pcg, MisuseIsAnError) {
    KernelPool pool(1);
    auto s = jacobiPcg();
    Vector x(3, 0.0);
    EXPECT_EQ(s->solve({1, 2, 3}, x, {}).error().code, ErrorCode::InvalidInput) << "no setup";
    const CsrMatrix K = laplacian1d(3);
    ASSERT_TRUE(s->setup(K, {}, pool, {}).ok());
    Vector wrong(2, 0.0);
    EXPECT_EQ(s->solve({1, 2, 3}, wrong, {}).error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(s->solve({1, std::numeric_limits<double>::quiet_NaN(), 3}, x, {}).error().code,
              ErrorCode::NonFinite);
    const CsrMatrix rect = uniforge::sparse::fromTriplets(2, 3, {}).value();
    EXPECT_EQ(s->setup(rect, {}, pool, {}).error().code, ErrorCode::InvalidInput);
}

TEST(SolverFactory, BuiltKindsExistOthersComeLater) {
    for (const SolverKind k :
         {SolverKind::PcgJacobi, SolverKind::PcgIc0, SolverKind::DirectND, SolverKind::DirectRCM}) {
        auto s = makeSolver(k);
        ASSERT_TRUE(s.ok()) << label(k);
        EXPECT_EQ(s.value()->kind(), k);
    }
    EXPECT_EQ(label(SolverKind::PcgJacobi), "PCG-J");
    for (const SolverKind k : {SolverKind::Amg, SolverKind::AmgPcg}) {
        auto r = makeSolver(k);
        ASSERT_FALSE(r.ok()) << label(k);
        EXPECT_EQ(r.error().code, ErrorCode::InvalidInput);
    }
}
