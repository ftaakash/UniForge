#include "core/solvers/external/HypreSolver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#if defined(UNIFORGE_HAVE_HYPRE)
#include <omp.h>

#include "HYPRE.h"
#include "HYPRE_IJ_mv.h"
#include "HYPRE_parcsr_ls.h"
#endif

#if defined(UNIFORGE_HAVE_HYPRE) && defined(_MSC_VER) && !defined(__clang__)
// MSVC's OpenMP runtime (vcomp, OpenMP 2.0) lacks omp_get_thread_limit (OpenMP 3.0), which hypre
// calls only in HYPRE_SetNumThreads to cap the requested count. Without nested parallelism the
// limit equals the maximum number of threads. (Alternative: /openmp:llvm, which needs the
// libomp140 runtime DLL.)
extern "C" int omp_get_thread_limit(void) {
    return omp_get_max_threads();
}
#endif

namespace uniforge::solvers {

namespace {
using Clock = std::chrono::steady_clock;
double secondsSince(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}
} // namespace

#if defined(UNIFORGE_HAVE_HYPRE)

struct HyprePcg::Impl {
    HYPRE_IJMatrix A = nullptr;
    HYPRE_IJVector b = nullptr;
    HYPRE_IJVector x = nullptr;
    HYPRE_Solver pcg = nullptr;
    HYPRE_Solver amg = nullptr;
    const CsrMatrix* K = nullptr;
    sparse::KernelPool* pool = nullptr;
    std::vector<HYPRE_BigInt> rows;
    std::size_t matrixBytes = 0;
    bool ready = false;

    ~Impl() {
        if (pcg != nullptr) {
            HYPRE_ParCSRPCGDestroy(pcg);
        }
        if (amg != nullptr) {
            HYPRE_BoomerAMGDestroy(amg);
        }
        if (x != nullptr) {
            HYPRE_IJVectorDestroy(x);
        }
        if (b != nullptr) {
            HYPRE_IJVectorDestroy(b);
        }
        if (A != nullptr) {
            HYPRE_IJMatrixDestroy(A);
        }
    }
};

namespace {
void ensureHypreInitialised() {
    static const bool done = [] {
        HYPRE_Initialize();
        return true;
    }();
    (void)done;
}

HYPRE_IJVector makeVector(HYPRE_BigInt n) {
    HYPRE_IJVector v = nullptr;
    HYPRE_IJVectorCreate(0, 0, n - 1, &v);
    HYPRE_IJVectorSetObjectType(v, HYPRE_PARCSR);
    HYPRE_IJVectorInitialize(v);
    return v;
}
} // namespace

bool HyprePcg::available() noexcept {
    return true;
}

HyprePcg::HyprePcg(SolverKind kind, double theta)
    : kind_(kind)
    , theta_(theta) {}

HyprePcg::~HyprePcg() = default;

Result<SetupReport> HyprePcg::setup(const CsrMatrix& K, const CoordinateHint& /*hint*/,
                                    sparse::KernelPool& pool, const SolverOptions& /*options*/) {
    if (K.rows != K.cols) {
        return makeError(ErrorCode::InvalidInput, "hypre PCG needs a square matrix.",
                         "This is a programming error in the caller.");
    }
    ensureHypreInitialised();
    omp_set_num_threads(pool.threads());
    impl_ = std::make_unique<Impl>();
    Impl& h = *impl_;
    h.K = &K;
    h.pool = &pool;
    SetupReport report;
    const auto t0 = Clock::now();

    // Copy K into hypre's IJ / ParCSR format (one process owns every row).
    const auto n = static_cast<HYPRE_BigInt>(K.rows);
    HYPRE_IJMatrixCreate(0, 0, n - 1, 0, n - 1, &h.A);
    HYPRE_IJMatrixSetObjectType(h.A, HYPRE_PARCSR);
    std::vector<HYPRE_Int> sizes(static_cast<std::size_t>(n));
    for (HYPRE_BigInt i = 0; i < n; ++i) {
        sizes[static_cast<std::size_t>(i)] = static_cast<HYPRE_Int>(
            K.rowPtr[static_cast<std::size_t>(i) + 1] - K.rowPtr[static_cast<std::size_t>(i)]);
    }
    HYPRE_IJMatrixSetRowSizes(h.A, sizes.data());
    HYPRE_IJMatrixInitialize(h.A);
    h.rows.resize(static_cast<std::size_t>(n));
    for (HYPRE_BigInt i = 0; i < n; ++i) {
        h.rows[static_cast<std::size_t>(i)] = i;
    }
    std::vector<HYPRE_BigInt> cols(K.col.begin(), K.col.end());
    HYPRE_IJMatrixSetValues(h.A, static_cast<HYPRE_Int>(n), sizes.data(), h.rows.data(),
                            cols.data(), K.val.data());
    HYPRE_IJMatrixAssemble(h.A);
    h.matrixBytes = K.col.size() * (sizeof(HYPRE_BigInt) + sizeof(double)) +
                    static_cast<std::size_t>(n) * sizeof(HYPRE_Int);
    h.b = makeVector(n);
    h.x = makeVector(n);
    HYPRE_IJVectorAssemble(h.b);
    HYPRE_IJVectorAssemble(h.x);
    report.phases.emplace_back("copy to hypre", secondsSince(t0));

    const auto ta = Clock::now();
    HYPRE_BoomerAMGCreate(&h.amg);
    HYPRE_BoomerAMGSetPrintLevel(h.amg, 0);
    HYPRE_BoomerAMGSetCoarsenType(h.amg, 10); // HMIS
    HYPRE_BoomerAMGSetInterpType(h.amg, 6);   // extended+i
    HYPRE_BoomerAMGSetPMaxElmts(h.amg, 4);
    HYPRE_BoomerAMGSetStrongThreshold(h.amg, theta_);
    HYPRE_BoomerAMGSetRelaxType(h.amg, 6); // hybrid symmetric Gauss–Seidel
    HYPRE_BoomerAMGSetNumSweeps(h.amg, 1);
    HYPRE_BoomerAMGSetMaxIter(h.amg, 1);
    HYPRE_BoomerAMGSetTol(h.amg, 0.0);

    HYPRE_ParCSRPCGCreate(0, &h.pcg);
    HYPRE_ParCSRPCGSetTwoNorm(h.pcg, 1);
    HYPRE_ParCSRPCGSetPrintLevel(h.pcg, 0);
    HYPRE_ParCSRPCGSetLogging(h.pcg, 1);
    HYPRE_ParCSRPCGSetPrecond(h.pcg, HYPRE_BoomerAMGSolve, HYPRE_BoomerAMGSetup, h.amg);

    HYPRE_ParCSRMatrix parA = nullptr;
    HYPRE_ParVector parB = nullptr;
    HYPRE_ParVector parX = nullptr;
    HYPRE_IJMatrixGetObject(h.A, reinterpret_cast<void**>(&parA));
    HYPRE_IJVectorGetObject(h.b, reinterpret_cast<void**>(&parB));
    HYPRE_IJVectorGetObject(h.x, reinterpret_cast<void**>(&parX));
    const HYPRE_Int err = HYPRE_ParCSRPCGSetup(h.pcg, parA, parB, parX);
    report.phases.emplace_back("BoomerAMG setup", secondsSince(ta));
    report.seconds = secondsSince(t0);
    if (err != 0) {
        report.status = SolveStatus::Breakdown;
        report.detail =
            "hypre PCG/BoomerAMG setup returned error code " + std::to_string(err) + ".";
        return report;
    }
    report.footprint = footprint();
    h.ready = true;
    return report;
}

Result<SolveReport> HyprePcg::solve(const Vector& f, Vector& x, const SolveControl& control) {
    if (!impl_ || !impl_->ready) {
        return makeError(ErrorCode::InvalidInput, "hypre PCG was not set up successfully.",
                         "Call setup() and check its status first.");
    }
    Impl& h = *impl_;
    const CsrMatrix& K = *h.K;
    const std::size_t n = static_cast<std::size_t>(K.rows);
    if (f.size() != n || x.size() != n) {
        return makeError(ErrorCode::InvalidInput,
                         "Right-hand side or initial guess does not match K.",
                         "This is a programming error in the caller.");
    }
    omp_set_num_threads(h.pool->threads());
    SolveReport rep;
    const auto t0 = Clock::now();
    const auto nn = static_cast<HYPRE_Int>(n);
    HYPRE_IJVectorSetValues(h.b, nn, h.rows.data(), f.data());
    HYPRE_IJVectorSetValues(h.x, nn, h.rows.data(), x.data());
    HYPRE_ParCSRMatrix parA = nullptr;
    HYPRE_ParVector parB = nullptr;
    HYPRE_ParVector parX = nullptr;
    HYPRE_IJMatrixGetObject(h.A, reinterpret_cast<void**>(&parA));
    HYPRE_IJVectorGetObject(h.b, reinterpret_cast<void**>(&parB));
    HYPRE_IJVectorGetObject(h.x, reinterpret_cast<void**>(&parX));
    HYPRE_ParCSRPCGSetTol(h.pcg, control.relTol);
    HYPRE_ParCSRPCGSetMaxIter(h.pcg, control.maxIterations);
    const HYPRE_Int err = HYPRE_ParCSRPCGSolve(h.pcg, parA, parB, parX);
    HYPRE_Int its = 0;
    HYPRE_Real rel = 0.0;
    HYPRE_ParCSRPCGGetNumIterations(h.pcg, &its);
    HYPRE_ParCSRPCGGetFinalRelativeResidualNorm(h.pcg, &rel);
    HYPRE_IJVectorGetValues(h.x, nn, h.rows.data(), x.data());
    rep.seconds = secondsSince(t0);
    rep.iterations = its;
    rep.residualHistory = {1.0, rel};
    if (err != 0 || !(rel <= control.relTol)) {
        HYPRE_ClearAllErrors();
        rep.status =
            its >= control.maxIterations ? SolveStatus::MaxIterations : SolveStatus::Breakdown;
        rep.detail = "hypre PCG returned error code " + std::to_string(err) +
                     " at relative residual " + std::to_string(rel) + ".";
    }
    // True residual with our own kernels, as for every other solver.
    Vector r(n);
    sparse::residual(*h.pool, K, x, f, r);
    const double normF = sparse::norm2(*h.pool, f);
    rep.finalTrueRelResidual = normF > 0.0 ? sparse::norm2(*h.pool, r) / normF : 0.0;
    if (control.residualSumMask != nullptr) {
        for (std::size_t i = 0; i < n; ++i) {
            if ((*control.residualSumMask)[i] != 0) {
                rep.residualSum += r[i];
            }
        }
    }
    return rep;
}

MemoryFootprint HyprePcg::footprint() const {
    MemoryFootprint f;
    if (impl_) {
        // Only the copy of K is known here; hypre does not report the hierarchy's bytes. The
        // run's peak working set (column peak_ws_bytes) covers everything.
        f.add("hypre copy of K (hierarchy not counted)", impl_->matrixBytes);
    }
    return f;
}

#else // hypre not built in

struct HyprePcg::Impl {};

bool HyprePcg::available() noexcept {
    return false;
}

HyprePcg::HyprePcg(SolverKind kind, double theta)
    : kind_(kind)
    , theta_(theta) {}

HyprePcg::~HyprePcg() = default;

Result<SetupReport> HyprePcg::setup(const CsrMatrix& /*K*/, const CoordinateHint& /*hint*/,
                                    sparse::KernelPool& /*pool*/,
                                    const SolverOptions& /*options*/) {
    return makeError(ErrorCode::InvalidInput, "This build has no hypre (reference solver).",
                     "Configure with -DUNIFORGE_WITH_HYPRE=ON -DHYPRE_ROOT=<install dir>.");
}

Result<SolveReport> HyprePcg::solve(const Vector& /*f*/, Vector& /*x*/,
                                    const SolveControl& /*control*/) {
    return makeError(ErrorCode::InvalidInput, "This build has no hypre (reference solver).",
                     "Configure with -DUNIFORGE_WITH_HYPRE=ON -DHYPRE_ROOT=<install dir>.");
}

MemoryFootprint HyprePcg::footprint() const {
    return {};
}

#endif

} // namespace uniforge::solvers
