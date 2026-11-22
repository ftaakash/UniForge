// Sparse direct solver (build spec §5.2, V2-5 exit criteria): orderings, symbolic prediction,
// L·Lᵀ = P·K·Pᵀ, agreement with the dense oracle and across orderings, budget and breakdown.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "ModelProblems.hpp"
#include "core/common/Rng.hpp"
#include "core/solvers/direct/Cholesky.hpp"
#include "support/DenseGauss.hpp"

using namespace uniforge::solvers;
using uniforge::ErrorCode;
using uniforge::sparse::CsrMatrix;
using uniforge::sparse::KernelPool;
using uniforge::sparse::Offset;
using uniforge::sparse::toSize;
using uniforge::testing::laplacian2d;
using uniforge::testing::laplacian3d;
using uniforge::testing::shuffled;

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

// Envelope bandwidth max |inverse[i] − inverse[j]| over the entries of A.
Index bandwidth(const CsrMatrix& A, const Permutation& p) {
    Index b = 0;
    for (Index r = 0; r < A.rows; ++r) {
        for (Offset k = A.rowPtr[toSize(r)]; k < A.rowPtr[toSize(r) + 1]; ++k) {
            b = std::max(b, static_cast<Index>(std::abs(p.inverse[toSize(r)] -
                                                        p.inverse[toSize(A.col[toSize(k)])])));
        }
    }
    return b;
}

struct Factored {
    std::unique_ptr<DirectCholesky> solver;
    SetupReport setup;
};

Factored factorise(const CsrMatrix& K, Ordering o, KernelPool& pool, SolverOptions opts = {}) {
    Factored f;
    f.solver = std::make_unique<DirectCholesky>(SolverKind::DirectND, o);
    auto r = f.solver->setup(K, {}, pool, opts);
    EXPECT_TRUE(r.ok());
    f.setup = r.value();
    return f;
}

} // namespace

// ---- Orderings --------------------------------------------------------------------------------

TEST(Ordering, EveryOrderingIsAValidPermutation) {
    const CsrMatrix A = shuffled(laplacian3d(9), 7);
    for (const Ordering o : {Ordering::Natural, Ordering::Rcm, Ordering::NestedDissection}) {
        const Permutation p = makeOrdering(o, A, 16);
        EXPECT_TRUE(isValid(p, A.rows)) << static_cast<int>(o);
    }
    EXPECT_FALSE(isValid(Permutation{{0, 0}, {0, 1}}, 2));
}

TEST(Ordering, RcmRecoversASmallBandwidthFromAShuffledGrid) {
    const CsrMatrix A = shuffled(laplacian2d(30), 11); // 900 nodes, random numbering
    const Index natural = bandwidth(A, naturalOrdering(A.rows));
    const Index rcm = bandwidth(A, rcmOrdering(A));
    EXPECT_GT(natural, 500);
    EXPECT_LE(rcm, 2 * 30 + 2) << "a 30 × 30 grid has a level structure about 30 nodes wide";
}

TEST(Ordering, HandlesDisconnectedGraphsAndIsolatedNodes) {
    // Two separate 2D grids plus an isolated node (diagonal only).
    std::vector<uniforge::sparse::Triplet> t;
    const CsrMatrix g = laplacian2d(6);
    for (Index r = 0; r < g.rows; ++r) {
        for (Offset k = g.rowPtr[toSize(r)]; k < g.rowPtr[toSize(r) + 1]; ++k) {
            t.push_back({r, g.col[toSize(k)], g.val[toSize(k)]});
            t.push_back({r + 36, g.col[toSize(k)] + 36, g.val[toSize(k)]});
        }
    }
    t.push_back({72, 72, 1.0});
    const CsrMatrix A = uniforge::sparse::fromTriplets(73, 73, t).value();
    EXPECT_TRUE(isValid(rcmOrdering(A), 73));
    EXPECT_TRUE(isValid(nestedDissectionOrdering(A, 4), 73));
}

TEST(Ordering, NestedDissectionReducesFillOnA3dGrid) {
    KernelPool pool(1);
    const CsrMatrix A = laplacian3d(16); // 4096 unknowns
    const Factored nat = factorise(A, Ordering::Natural, pool);
    const Factored rcm = factorise(A, Ordering::Rcm, pool);
    const Factored nd = factorise(A, Ordering::NestedDissection, pool);
    ASSERT_EQ(nd.setup.status, SolveStatus::Converged);
    std::printf("[fill 16³ grid] nnz(L): natural %lld, RCM %lld, ND %lld\n",
                static_cast<long long>(nat.setup.factorNnz),
                static_cast<long long>(rcm.setup.factorNnz),
                static_cast<long long>(nd.setup.factorNnz));
    EXPECT_LT(nd.setup.factorNnz, rcm.setup.factorNnz);
    EXPECT_LT(nd.setup.factorFlops, rcm.setup.factorFlops);
}

// ---- Cholesky ---------------------------------------------------------------------------------

TEST(DirectCholesky, SymbolicPredictionIsExactAndLTimesLTransposeIsPKP) {
    KernelPool pool(1);
    const CsrMatrix A = shuffled(laplacian2d(9), 3);
    for (const Ordering o : {Ordering::Natural, Ordering::Rcm, Ordering::NestedDissection}) {
        const Factored f = factorise(A, o, pool);
        ASSERT_EQ(f.setup.status, SolveStatus::Converged);
        EXPECT_EQ(f.setup.factorNnz, f.setup.predictedFactorNnz);
        const auto& L = f.solver->factor();
        const std::size_t n = toSize(A.rows);
        // Dense L·Lᵀ vs dense P·A·Pᵀ.
        std::vector<double> Ld(n * n, 0.0);
        for (std::size_t j = 0; j < n; ++j) {
            ASSERT_EQ(L.rowIdx[toSize(L.colPtr[j])], static_cast<Index>(j)) << "diagonal first";
            for (auto q = L.colPtr[j]; q < L.colPtr[j + 1]; ++q) {
                Ld[toSize(L.rowIdx[toSize(q)]) * n + j] = L.val[toSize(q)];
            }
        }
        const auto Ad = toDense(A);
        const auto& p = L.permutation.perm;
        for (std::size_t r = 0; r < n; ++r) {
            for (std::size_t c = 0; c < n; ++c) {
                double llt = 0.0;
                for (std::size_t k = 0; k < n; ++k) {
                    llt += Ld[r * n + k] * Ld[c * n + k];
                }
                const double pap = Ad[toSize(p[r]) * n + toSize(p[c])];
                ASSERT_NEAR(llt, pap, 1e-12) << "(" << r << ", " << c << ")";
            }
        }
    }
}

TEST(DirectCholesky, SolvesLikeTheDenseOracleWithEveryOrdering) {
    KernelPool pool(2);
    const CsrMatrix A = shuffled(laplacian3d(7), 5); // 343 unknowns
    const Vector f = randomRhs(343, 9);
    const Vector ref = denseReference(A, f);
    for (const Ordering o : {Ordering::Natural, Ordering::Rcm, Ordering::NestedDissection}) {
        Factored fa = factorise(A, o, pool);
        Vector x(343, 0.0);
        auto r = fa.solver->solve(f, x, {});
        ASSERT_TRUE(r.ok());
        EXPECT_EQ(r.value().status, SolveStatus::Converged);
        EXPECT_EQ(r.value().iterations, 0);
        EXPECT_LT(r.value().finalTrueRelResidual, 1e-14);
        for (std::size_t i = 0; i < ref.size(); ++i) {
            ASSERT_NEAR(x[i], ref[i], 1e-12 * (1.0 + std::fabs(ref[i]))) << static_cast<int>(o);
        }
    }
}

TEST(DirectCholesky, FactorOnceSolveManyAndReportFootprint) {
    KernelPool pool(1);
    const CsrMatrix A = laplacian2d(20);
    Factored fa = factorise(A, Ordering::NestedDissection, pool);
    EXPECT_GT(fa.setup.footprint.total(), 0U);
    ASSERT_EQ(fa.setup.phases.size(), 3U);
    EXPECT_EQ(fa.setup.phases[0].first, "ordering");
    EXPECT_EQ(fa.setup.phases[2].first, "numeric");
    for (std::uint64_t seed = 20; seed < 23; ++seed) {
        const Vector f = randomRhs(400, seed);
        Vector x(400, 0.0);
        auto r = fa.solver->solve(f, x, {});
        ASSERT_TRUE(r.ok());
        EXPECT_LT(r.value().finalTrueRelResidual, 1e-13);
    }
}

TEST(DirectCholesky, OverBudgetIsDetectedBeforeTheFactorIsAllocated) {
    KernelPool pool(1);
    const CsrMatrix A = laplacian3d(12);
    SolverOptions tight;
    tight.memoryBudgetBytes = 100'000; // far below what L needs
    const Factored f = factorise(A, Ordering::NestedDissection, pool, tight);
    EXPECT_EQ(f.setup.status, SolveStatus::OverBudget);
    EXPECT_GT(f.setup.predictedFactorNnz, 0);
    EXPECT_EQ(f.setup.factorNnz, 0);
    EXPECT_TRUE(f.solver->factor().val.empty()) << "no factor allocated";
    Vector x(toSize(A.rows), 0.0);
    EXPECT_EQ(f.solver->solve(Vector(toSize(A.rows), 1.0), x, {}).error().code,
              ErrorCode::InvalidInput);
}

TEST(DirectCholesky, IndefiniteOrSingularMatrixIsABreakdown) {
    KernelPool pool(1);
    // Graph Laplacian of a path without any boundary: singular (constant vector in the kernel).
    std::vector<uniforge::sparse::Triplet> t;
    for (Index i = 0; i < 10; ++i) {
        const double deg = (i == 0 || i == 9) ? 1.0 : 2.0;
        t.push_back({i, i, deg});
        if (i > 0) {
            t.push_back({i, i - 1, -1.0});
            t.push_back({i - 1, i, -1.0});
        }
    }
    const CsrMatrix singular = uniforge::sparse::fromTriplets(10, 10, t).value();
    const Factored f = factorise(singular, Ordering::Natural, pool);
    EXPECT_EQ(f.setup.status, SolveStatus::Breakdown) << f.setup.detail;
    const CsrMatrix indefinite =
        uniforge::sparse::fromTriplets(2, 2, {{0, 0, 1.0}, {1, 1, -1.0}}).value();
    EXPECT_EQ(factorise(indefinite, Ordering::Rcm, pool).setup.status, SolveStatus::Breakdown);
}

TEST(DirectCholesky, MisuseIsAnError) {
    KernelPool pool(1);
    DirectCholesky s(SolverKind::DirectRCM, Ordering::Rcm);
    Vector x(3, 0.0);
    EXPECT_EQ(s.solve({1, 2, 3}, x, {}).error().code, ErrorCode::InvalidInput);
    const CsrMatrix rect = uniforge::sparse::fromTriplets(2, 3, {}).value();
    EXPECT_EQ(s.setup(rect, {}, pool, {}).error().code, ErrorCode::InvalidInput);
    const CsrMatrix A = laplacian2d(3);
    ASSERT_TRUE(s.setup(A, {}, pool, {}).ok());
    Vector wrong(2, 0.0);
    EXPECT_EQ(s.solve(Vector(9, 1.0), wrong, {}).error().code, ErrorCode::InvalidInput);
    Vector nine(9, 0.0);
    Vector bad(9, 1.0);
    bad[4] = std::numeric_limits<double>::infinity();
    EXPECT_EQ(s.solve(bad, nine, {}).error().code, ErrorCode::NonFinite);
}

TEST(DirectCholesky, NumericallySingularMatrixIsABreakdownNotAnAnswer) {
    // Graph Laplacian with irrational edge weights and zero row sums: singular, but rounding
    // leaves the last pivot slightly nonzero, so a plain "pivot <= 0" test can miss it (E8 dry
    // run, all-insulated HS-7: the direct solvers returned 2e13 degC).
    KernelPool pool(1);
    const Index m = 12;
    std::vector<uniforge::sparse::Triplet> t;
    std::vector<double> diag(toSize(m * m), 0.0);
    const auto edge = [&](Index a, Index b, double w) {
        t.push_back({a, b, -w});
        t.push_back({b, a, -w});
        diag[toSize(a)] += w;
        diag[toSize(b)] += w;
    };
    for (Index j = 0; j < m; ++j) {
        for (Index i = 0; i < m; ++i) {
            const Index id = i + m * j;
            if (i + 1 < m) {
                edge(id, id + 1, std::sqrt(2.0) + 0.1 * i);
            }
            if (j + 1 < m) {
                edge(id, id + m, std::sqrt(3.0) + 0.07 * j);
            }
        }
    }
    for (Index r = 0; r < m * m; ++r) {
        t.push_back({r, r, diag[toSize(r)]});
    }
    const CsrMatrix A = uniforge::sparse::fromTriplets(m * m, m * m, t).value();
    for (const Ordering o : {Ordering::Natural, Ordering::Rcm, Ordering::NestedDissection}) {
        const Factored f = factorise(A, o, pool);
        EXPECT_EQ(f.setup.status, SolveStatus::Breakdown)
            << static_cast<int>(o) << ": " << f.setup.detail;
    }
}
