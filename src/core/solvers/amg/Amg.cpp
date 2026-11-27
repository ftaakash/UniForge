#include "core/solvers/amg/Amg.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

#include "core/solvers/Observers.hpp"
#include "core/sparse/SparseOps.hpp"

namespace uniforge::solvers {

namespace {

using Clock = std::chrono::steady_clock;
using sparse::Offset;
using sparse::toSize;

double secondsSince(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

std::string number(double v) {
    std::ostringstream s;
    s.precision(3);
    s << v;
    return s.str();
}

// Bytes read and written by one pass over a CSR matrix with vector in and out (SpMV or a sweep).
double passBytes(const CsrMatrix& A) {
    return 12.0 * static_cast<double>(A.nnz()) + 8.0 * static_cast<double>(A.rows + 1) +
           16.0 * static_cast<double>(std::max(A.rows, A.cols));
}

// The same pass with float values and float vectors (V3a): 4 + 4 bytes per entry.
double passBytesSingle(const CsrMatrix& A) {
    return 8.0 * static_cast<double>(A.nnz()) + 8.0 * static_cast<double>(A.rows + 1) +
           8.0 * static_cast<double>(std::max(A.rows, A.cols));
}

std::vector<float> toFloat(const std::vector<double>& v, sparse::KernelPool& pool) {
    std::vector<float> out(v.size());
    pool.parallelFor(static_cast<Index>(v.size()), [&](Index begin, Index end) {
        for (Index i = begin; i < end; ++i) {
            out[toSize(i)] = static_cast<float>(v[toSize(i)]);
        }
    });
    return out;
}

void releaseValues(CsrMatrix& A) {
    A.val.clear();
    A.val.shrink_to_fit();
}

// Gauss–Seidel on float data. The row sum is accumulated in double (cheap: the cost of a sweep
// is the memory traffic) so the only rounding to float is the stored update.
template <class FloatCsr>
void gaussSeidelSingle(const FloatCsr& A, const std::vector<float>& b, std::vector<float>& x,
                       bool forward) {
    const Index n = A.rows;
    const auto& rowPtr = *A.rowPtr;
    const auto& col = *A.col;
    for (Index step = 0; step < n; ++step) {
        const Index i = forward ? step : n - 1 - step;
        double s = b[toSize(i)];
        double d = 0.0;
        for (Offset q = rowPtr[toSize(i)]; q < rowPtr[toSize(i) + 1]; ++q) {
            const Index j = col[toSize(q)];
            if (j == i) {
                d = A.val[toSize(q)];
            } else {
                s -= static_cast<double>(A.val[toSize(q)]) * x[toSize(j)];
            }
        }
        x[toSize(i)] = static_cast<float>(s / d);
    }
}

// y ← A·x (or y ← b − A·x when b is given), split into the pool's chunks like sparse::spmv.
template <class FloatCsr>
void spmvSingle(sparse::KernelPool& pool, const FloatCsr& A, const std::vector<float>& x,
                std::vector<float>& y, const std::vector<float>* b = nullptr) {
    const auto& rowPtr = *A.rowPtr;
    const auto& col = *A.col;
    pool.parallelFor(A.rows, [&](Index begin, Index end) {
        for (Index i = begin; i < end; ++i) {
            double s = 0.0;
            for (Offset q = rowPtr[toSize(i)]; q < rowPtr[toSize(i) + 1]; ++q) {
                s += static_cast<double>(A.val[toSize(q)]) * x[toSize(col[toSize(q)])];
            }
            y[toSize(i)] = static_cast<float>(b != nullptr ? (*b)[toSize(i)] - s : s);
        }
    });
}

// Chebyshev–Jacobi smoother (V3b), values stored as T (double or float), sums in double.
// Chebyshev acceleration of x ← x + D⁻¹(b − A·x) for eigenvalues of D⁻¹A in [λ̂/ratio, λ̂]
// (Saad, Iterative Methods for Sparse Linear Systems, 2nd ed., Alg. 12.1), starting from the
// given x. Its error propagator I − p(D⁻¹A)·D⁻¹A is A-self-adjoint, so the same smoother before
// and after the coarse correction keeps the V-cycle symmetric. Every pass is a parallel loop
// over rows; the update of x is a separate pass because the next residual reads neighbours.
template <class T>
void chebyshev(sparse::KernelPool& pool, Index n, const Offset* rowPtr, const Index* col,
               const T* val, const T* invD, const T* b, T* x, T* r, T* d, double lambdaMax,
               double ratio, int degree) {
    const double beta = lambdaMax;
    const double alpha = lambdaMax / ratio;
    const double theta = 0.5 * (beta + alpha);
    const double delta = 0.5 * (beta - alpha);
    const double sigma = theta / delta;
    double rho = 1.0 / sigma;
    const auto rowResidual = [&](Index i) {
        double s = b[i];
        for (Offset q = rowPtr[i]; q < rowPtr[i + 1]; ++q) {
            s -= static_cast<double>(val[q]) * static_cast<double>(x[col[q]]);
        }
        return s;
    };
    pool.parallelFor(n, [&](Index begin, Index end) {
        for (Index i = begin; i < end; ++i) {
            const double ri = rowResidual(i);
            r[i] = static_cast<T>(ri);
            d[i] = static_cast<T>(static_cast<double>(invD[i]) * ri / theta);
        }
    });
    for (int k = 1; k <= degree; ++k) {
        pool.parallelFor(n, [&](Index begin, Index end) {
            for (Index i = begin; i < end; ++i) {
                x[i] = static_cast<T>(static_cast<double>(x[i]) + static_cast<double>(d[i]));
            }
        });
        if (k == degree) {
            break;
        }
        const double rhoNew = 1.0 / (2.0 * sigma - rho);
        const double c1 = rhoNew * rho;
        const double c2 = 2.0 * rhoNew / delta;
        pool.parallelFor(n, [&](Index begin, Index end) {
            for (Index i = begin; i < end; ++i) {
                const double ri = rowResidual(i);
                r[i] = static_cast<T>(ri);
                d[i] = static_cast<T>(c1 * static_cast<double>(d[i]) +
                                      c2 * static_cast<double>(invD[i]) * ri);
            }
        });
        rho = rhoNew;
    }
}

// Bytes of one Chebyshev application: degree residual passes (each also writes r and d and
// reads 1/a_ii) and degree updates of x (read x and d, write x). `bytes` = 8 (double) or 4.
double chebyshevBytes(const CsrMatrix& A, int degree, double valueBytes) {
    const double nnz = static_cast<double>(A.nnz());
    const double n = static_cast<double>(A.rows);
    const double residualPass = (4.0 + valueBytes) * nnz + 8.0 * (n + 1.0) +
                                valueBytes * n * 5.0; // x gathered, b, 1/a_ii, d, r written
    const double updatePass = 3.0 * valueBytes * n;
    return degree * (residualPass + updatePass);
}

// ---- Block kernels (V3e): k cases per row, interleaved (row i, case c at i·k + c) ----------

// Y ← A·X, or Y ← B − A·X when B is given. Matrix values of type T, vectors of type V; sums in
// double. Each matrix entry is read once for all k cases.
template <class T, class V>
void blockSpmv(sparse::KernelPool& pool, Index n, const Offset* rowPtr, const Index* col,
               const T* val, const V* X, V* Y, int k, const V* B = nullptr) {
    pool.parallelFor(n, [&](Index begin, Index end) {
        std::array<double, AmgHierarchy::kMaxBlock> acc{};
        for (Index i = begin; i < end; ++i) {
            std::fill(acc.begin(), acc.begin() + k, 0.0);
            for (Offset q = rowPtr[i]; q < rowPtr[i + 1]; ++q) {
                const double a = static_cast<double>(val[q]);
                const V* x = X + static_cast<std::size_t>(col[q]) * toSize(k);
                for (int c = 0; c < k; ++c) {
                    acc[toSize(c)] += a * static_cast<double>(x[c]);
                }
            }
            V* y = Y + toSize(i) * toSize(k);
            const V* b = B != nullptr ? B + toSize(i) * toSize(k) : nullptr;
            for (int c = 0; c < k; ++c) {
                y[c] = static_cast<V>(b != nullptr ? static_cast<double>(b[c]) - acc[toSize(c)]
                                                   : acc[toSize(c)]);
            }
        }
    });
}

// chebyshev<T> for k interleaved cases (same polynomial, same 1/a_ii for every case).
template <class T>
void chebyshevBlock(sparse::KernelPool& pool, Index n, const Offset* rowPtr, const Index* col,
                    const T* val, const T* invD, const T* b, T* x, T* r, T* d, int k,
                    double lambdaMax, double ratio, int degree) {
    const double beta = lambdaMax;
    const double alpha = lambdaMax / ratio;
    const double theta = 0.5 * (beta + alpha);
    const double delta = 0.5 * (beta - alpha);
    const double sigma = theta / delta;
    double rho = 1.0 / sigma;
    const std::size_t kk = toSize(k);
    // r ← b − A·x for the rows [begin, end), then d from r with coefficients (c1, c2).
    const auto residualAndDirection = [&](double c1, double c2, bool first) {
        pool.parallelFor(n, [&](Index begin, Index end) {
            std::array<double, AmgHierarchy::kMaxBlock> acc{};
            for (Index i = begin; i < end; ++i) {
                const std::size_t row = toSize(i) * kk;
                for (std::size_t c = 0; c < kk; ++c) {
                    acc[c] = static_cast<double>(b[row + c]);
                }
                for (Offset q = rowPtr[i]; q < rowPtr[i + 1]; ++q) {
                    const double a = static_cast<double>(val[q]);
                    const T* xj = x + toSize(col[q]) * kk;
                    for (std::size_t c = 0; c < kk; ++c) {
                        acc[c] -= a * static_cast<double>(xj[c]);
                    }
                }
                const double inv = static_cast<double>(invD[i]);
                for (std::size_t c = 0; c < kk; ++c) {
                    r[row + c] = static_cast<T>(acc[c]);
                    d[row + c] = static_cast<T>(
                        (first ? 0.0 : c1 * static_cast<double>(d[row + c])) + c2 * inv * acc[c]);
                }
            }
        });
    };
    residualAndDirection(0.0, 1.0 / theta, true);
    const Index total = static_cast<Index>(toSize(n) * kk);
    for (int s = 1; s <= degree; ++s) {
        pool.parallelFor(total, [&](Index begin, Index end) {
            for (Index i = begin; i < end; ++i) {
                x[i] = static_cast<T>(static_cast<double>(x[i]) + static_cast<double>(d[i]));
            }
        });
        if (s == degree) {
            break;
        }
        const double rhoNew = 1.0 / (2.0 * sigma - rho);
        residualAndDirection(rhoNew * rho, 2.0 * rhoNew / delta, false);
        rho = rhoNew;
    }
}

// Per-case sums Σ_i f(i, c) over n rows, chunk by chunk in a fixed order: bit-identical for any
// thread count. `partial(i, sums)` adds row i's k contributions to sums.
template <class F>
std::vector<double> blockSums(sparse::KernelPool& pool, Index n, int k, const F& partial) {
    const Index chunks = (n + sparse::KernelPool::kChunkRows - 1) / sparse::KernelPool::kChunkRows;
    std::vector<double> part(toSize(chunks) * toSize(k), 0.0);
    pool.parallelFor(n, [&](Index begin, Index end) {
        double* s = part.data() + toSize(begin / sparse::KernelPool::kChunkRows) * toSize(k);
        for (Index i = begin; i < end; ++i) {
            partial(i, s);
        }
    });
    std::vector<double> sums(toSize(k), 0.0);
    for (Index c = 0; c < chunks; ++c) {
        for (int j = 0; j < k; ++j) {
            sums[toSize(j)] += part[toSize(c) * toSize(k) + toSize(j)];
        }
    }
    return sums;
}

void gaussSeidel(const CsrMatrix& A, const Vector& b, Vector& x, bool forward) {
    const Index n = A.rows;
    for (Index step = 0; step < n; ++step) {
        const Index i = forward ? step : n - 1 - step;
        double s = b[toSize(i)];
        double d = 0.0;
        for (Offset q = A.rowPtr[toSize(i)]; q < A.rowPtr[toSize(i) + 1]; ++q) {
            const Index j = A.col[toSize(q)];
            if (j == i) {
                d = A.val[toSize(q)];
            } else {
                s -= A.val[toSize(q)] * x[toSize(j)];
            }
        }
        x[toSize(i)] = s / d;
    }
}

} // namespace

// ---- Building blocks --------------------------------------------------------------------------

namespace {

// Row loop body(begin, end) over [0, n): on the pool if given, else in one call.
void forRows(sparse::KernelPool* pool, Index n, const std::function<void(Index, Index)>& body) {
    if (pool != nullptr) {
        pool->parallelFor(n, body);
    } else {
        body(0, n);
    }
}

} // namespace

std::vector<char> strongConnections(const CsrMatrix& A, double theta, sparse::KernelPool* pool) {
    sparse::Vector d(toSize(A.rows), 0.0);
    forRows(pool, A.rows, [&](Index begin, Index end) {
        for (Index i = begin; i < end; ++i) {
            d[toSize(i)] = A.at(i, i);
        }
    });
    std::vector<char> strong(toSize(A.nnz()), 0);
    forRows(pool, A.rows, [&](Index begin, Index end) {
        for (Index i = begin; i < end; ++i) {
            for (Offset q = A.rowPtr[toSize(i)]; q < A.rowPtr[toSize(i) + 1]; ++q) {
                const Index j = A.col[toSize(q)];
                if (j == i) {
                    continue;
                }
                const double a = std::fabs(A.val[toSize(q)]);
                strong[toSize(q)] =
                    a > 0.0 && a >= theta * std::sqrt(std::fabs(d[toSize(i)] * d[toSize(j)])) ? 1
                                                                                              : 0;
            }
        }
    });
    return strong;
}

std::vector<Index> aggregate(const CsrMatrix& A, const std::vector<char>& strong, Index& count) {
    const Index n = A.rows;
    std::vector<Index> agg(toSize(n), -1);
    std::vector<char> isolated(toSize(n), 1);
    for (Index i = 0; i < n; ++i) {
        for (Offset q = A.rowPtr[toSize(i)]; q < A.rowPtr[toSize(i) + 1]; ++q) {
            if (strong[toSize(q)] != 0) {
                isolated[toSize(i)] = 0;
                break;
            }
        }
    }
    count = 0;
    // (a) Roots: every strong neighbour still free.
    for (Index i = 0; i < n; ++i) {
        if (isolated[toSize(i)] != 0 || agg[toSize(i)] >= 0) {
            continue;
        }
        bool allFree = true;
        for (Offset q = A.rowPtr[toSize(i)]; q < A.rowPtr[toSize(i) + 1] && allFree; ++q) {
            if (strong[toSize(q)] != 0 && agg[toSize(A.col[toSize(q)])] >= 0) {
                allFree = false;
            }
        }
        if (!allFree) {
            continue;
        }
        agg[toSize(i)] = count;
        for (Offset q = A.rowPtr[toSize(i)]; q < A.rowPtr[toSize(i) + 1]; ++q) {
            const Index j = A.col[toSize(q)];
            if (strong[toSize(q)] != 0 && isolated[toSize(j)] == 0) {
                agg[toSize(j)] = count;
            }
        }
        ++count;
    }
    // (b) Join the strongest neighbouring aggregate from (a).
    const std::vector<Index> afterRoots = agg;
    for (Index i = 0; i < n; ++i) {
        if (isolated[toSize(i)] != 0 || agg[toSize(i)] >= 0) {
            continue;
        }
        double best = 0.0;
        for (Offset q = A.rowPtr[toSize(i)]; q < A.rowPtr[toSize(i) + 1]; ++q) {
            const Index j = A.col[toSize(q)];
            if (strong[toSize(q)] != 0 && afterRoots[toSize(j)] >= 0 &&
                std::fabs(A.val[toSize(q)]) > best) {
                best = std::fabs(A.val[toSize(q)]);
                agg[toSize(i)] = afterRoots[toSize(j)];
            }
        }
    }
    // (c) Leftovers: new aggregates with their free strong neighbours.
    for (Index i = 0; i < n; ++i) {
        if (isolated[toSize(i)] != 0 || agg[toSize(i)] >= 0) {
            continue;
        }
        agg[toSize(i)] = count;
        for (Offset q = A.rowPtr[toSize(i)]; q < A.rowPtr[toSize(i) + 1]; ++q) {
            const Index j = A.col[toSize(q)];
            if (strong[toSize(q)] != 0 && agg[toSize(j)] < 0 && isolated[toSize(j)] == 0) {
                agg[toSize(j)] = count;
            }
        }
        ++count;
    }
    return agg;
}

CsrMatrix tentativeProlongator(const std::vector<Index>& aggregateOf, Index count) {
    CsrMatrix P;
    P.rows = static_cast<Index>(aggregateOf.size());
    P.cols = count;
    P.rowPtr.assign(aggregateOf.size() + 1, 0);
    for (std::size_t i = 0; i < aggregateOf.size(); ++i) {
        P.rowPtr[i + 1] = P.rowPtr[i] + (aggregateOf[i] >= 0 ? 1 : 0);
        if (aggregateOf[i] >= 0) {
            P.col.push_back(aggregateOf[i]);
            P.val.push_back(1.0);
        }
    }
    return P;
}

CsrMatrix smoothedProlongator(const CsrMatrix& A, const std::vector<char>& strong,
                              const CsrMatrix& P0, sparse::KernelPool* pool) {
    // Filtered matrix A_F: strong couplings and the diagonal; weak couplings lumped onto it.
    // Two passes over the rows (count, then fill) so each row is written independently; A's
    // columns are sorted, so the diagonal goes in before the first column > i.
    CsrMatrix F;
    F.rows = A.rows;
    F.cols = A.cols;
    F.rowPtr.assign(toSize(A.rows) + 1, 0);
    std::vector<double> dF(toSize(A.rows), 0.0);
    forRows(pool, A.rows, [&](Index begin, Index end) {
        for (Index i = begin; i < end; ++i) {
            Offset count = 1; // the diagonal
            double diag = 0.0;
            double lumped = 0.0;
            for (Offset q = A.rowPtr[toSize(i)]; q < A.rowPtr[toSize(i) + 1]; ++q) {
                const Index j = A.col[toSize(q)];
                if (j == i) {
                    diag = A.val[toSize(q)];
                } else if (strong[toSize(q)] != 0) {
                    ++count;
                } else {
                    lumped += A.val[toSize(q)];
                }
            }
            // Keep a usable diagonal.
            dF[toSize(i)] = diag + lumped > 0.0 ? diag + lumped : diag;
            F.rowPtr[toSize(i) + 1] = count;
        }
    });
    for (std::size_t i = 0; i < toSize(A.rows); ++i) {
        F.rowPtr[i + 1] += F.rowPtr[i];
    }
    F.col.resize(toSize(F.nnz()));
    F.val.resize(toSize(F.nnz()));
    forRows(pool, A.rows, [&](Index begin, Index end) {
        for (Index i = begin; i < end; ++i) {
            std::size_t at = toSize(F.rowPtr[toSize(i)]);
            bool diagonalWritten = false;
            const auto writeDiagonal = [&] {
                F.col[at] = i;
                F.val[at] = dF[toSize(i)];
                ++at;
                diagonalWritten = true;
            };
            for (Offset q = A.rowPtr[toSize(i)]; q < A.rowPtr[toSize(i) + 1]; ++q) {
                const Index j = A.col[toSize(q)];
                if (j == i || strong[toSize(q)] == 0) {
                    continue;
                }
                if (!diagonalWritten && j > i) {
                    writeDiagonal();
                }
                F.col[at] = j;
                F.val[at] = A.val[toSize(q)];
                ++at;
            }
            if (!diagonalWritten) {
                writeDiagonal();
            }
        }
    });
    // ρ(D_F⁻¹·A_F) ≤ max_i Σ_j |F_ij| / |d_i| (Gershgorin).
    const auto rowBound = [&](Index begin, Index end) {
        double rho = 0.0;
        for (Index i = begin; i < end; ++i) {
            double row = 0.0;
            for (Offset q = F.rowPtr[toSize(i)]; q < F.rowPtr[toSize(i) + 1]; ++q) {
                row += std::fabs(F.val[toSize(q)]);
            }
            rho = std::max(rho, row / std::fabs(dF[toSize(i)]));
        }
        return rho;
    };
    const double rho =
        pool != nullptr ? std::max(0.0, pool->reduceMax(F.rows, rowBound)) : rowBound(0, F.rows);
    const double omega = rho > 0.0 ? 4.0 / (3.0 * rho) : 0.0;

    // Pattern ⊇ P0 (the diagonal of F is stored).
    auto FP = pool != nullptr ? sparse::multiply(*pool, F, P0) : sparse::multiply(F, P0);
    CsrMatrix P = std::move(FP).value();
    forRows(pool, P.rows, [&](Index begin, Index end) {
        for (Index i = begin; i < end; ++i) {
            const double scale = omega / dF[toSize(i)];
            for (Offset q = P.rowPtr[toSize(i)]; q < P.rowPtr[toSize(i) + 1]; ++q) {
                P.val[toSize(q)] = -scale * P.val[toSize(q)];
            }
            for (Offset q = P0.rowPtr[toSize(i)]; q < P0.rowPtr[toSize(i) + 1]; ++q) {
                const Offset at = P.find(i, P0.col[toSize(q)]);
                P.val[toSize(at)] += P0.val[toSize(q)];
            }
        }
    });
    return P;
}

CsrMatrix geometricProlongator(const CsrMatrix& A,
                               const std::vector<std::array<std::int32_t, 3>>& grid,
                               std::int32_t stride, const std::array<std::vector<double>, 3>& lines,
                               std::vector<std::array<std::int32_t, 3>>& coarseGrid,
                               sparse::KernelPool* pool) {
    const Index n = A.rows;
    std::array<std::int64_t, 3> dims{};
    for (std::size_t a = 0; a < 3; ++a) {
        dims[a] = static_cast<std::int64_t>(lines[a].size());
    }
    const auto pointOf = [&](std::int64_t i, std::int64_t j, std::int64_t k) {
        return i + dims[0] * (j + dims[1] * k);
    };
    // Node of each aligned grid point (all indices multiples of stride), −1 elsewhere.
    std::vector<std::int32_t> alignedNode(static_cast<std::size_t>(dims[0] * dims[1] * dims[2]),
                                          -1);
    // role: 0 = empty row (decoupled), 1 = coarse (aligned or promoted), 2 = interpolated.
    std::vector<char> role(toSize(n), 2);
    forRows(pool, n, [&](Index begin, Index end) {
        for (Index v = begin; v < end; ++v) {
            const auto& g = grid[toSize(v)];
            if (A.rowPtr[toSize(v) + 1] - A.rowPtr[toSize(v)] <= 1) {
                role[toSize(v)] = 0;
            } else if (g[0] % stride == 0 && g[1] % stride == 0 && g[2] % stride == 0) {
                role[toSize(v)] = 1;
                alignedNode[static_cast<std::size_t>(pointOf(g[0], g[1], g[2]))] = v;
            }
        }
    });
    // The corners of node v's stride box with their trilinear weights; corners that are not
    // aligned coupled nodes are skipped. Returns the number written to out (≤ 8).
    struct Corner {
        Index node;
        double w;
    };
    const auto corners = [&](Index v, std::array<Corner, 8>& out) {
        const auto& g = grid[toSize(v)];
        std::array<std::array<std::pair<std::int32_t, double>, 2>, 3> axis{};
        std::array<std::size_t, 3> count{};
        for (std::size_t a = 0; a < 3; ++a) {
            const std::int32_t lo = g[a] - g[a] % stride;
            const std::int32_t hi = lo + stride;
            if (lo == g[a] || hi >= static_cast<std::int32_t>(dims[a])) {
                axis[a][0] = {lo, 1.0};
                count[a] = 1;
            } else {
                const double x = lines[a][toSize(g[a])];
                const double xl = lines[a][toSize(lo)];
                const double xh = lines[a][toSize(hi)];
                const double wl = (xh - x) / (xh - xl);
                axis[a][0] = {lo, wl};
                axis[a][1] = {hi, 1.0 - wl};
                count[a] = 2;
            }
        }
        int m = 0;
        for (std::size_t c = 0; c < count[2]; ++c) {
            for (std::size_t b = 0; b < count[1]; ++b) {
                for (std::size_t a = 0; a < count[0]; ++a) {
                    const std::int32_t u = alignedNode[static_cast<std::size_t>(
                        pointOf(axis[0][a].first, axis[1][b].first, axis[2][c].first))];
                    if (u >= 0 && role[toSize(u)] == 1) {
                        out[toSize(m)] = {u, axis[0][a].second * axis[1][b].second *
                                                 axis[2][c].second};
                        ++m;
                    }
                }
            }
        }
        return m;
    };
    // Promote interpolated nodes without any corner. Promotion only reads aligned nodes, so the
    // order of the rows does not matter.
    std::vector<char> promote(toSize(n), 0);
    forRows(pool, n, [&](Index begin, Index end) {
        std::array<Corner, 8> c{};
        for (Index v = begin; v < end; ++v) {
            if (role[toSize(v)] == 2 && corners(v, c) == 0) {
                promote[toSize(v)] = 1;
            }
        }
    });
    // Coarse numbering in fine-node order.
    std::vector<Index> coarseOf(toSize(n), -1);
    Index count = 0;
    coarseGrid.clear();
    for (Index v = 0; v < n; ++v) {
        if (role[toSize(v)] == 1 || promote[toSize(v)] != 0) {
            coarseOf[toSize(v)] = count++;
            coarseGrid.push_back(grid[toSize(v)]);
        }
    }
    // P: identity rows for coarse nodes, renormalised trilinear rows for the others.
    CsrMatrix P;
    P.rows = n;
    P.cols = count;
    P.rowPtr.assign(toSize(n) + 1, 0);
    forRows(pool, n, [&](Index begin, Index end) {
        std::array<Corner, 8> c{};
        for (Index v = begin; v < end; ++v) {
            Offset len = 0;
            if (coarseOf[toSize(v)] >= 0) {
                len = 1;
            } else if (role[toSize(v)] == 2) {
                len = corners(v, c);
            }
            P.rowPtr[toSize(v) + 1] = len;
        }
    });
    for (std::size_t i = 0; i < toSize(n); ++i) {
        P.rowPtr[i + 1] += P.rowPtr[i];
    }
    P.col.resize(toSize(P.nnz()));
    P.val.resize(toSize(P.nnz()));
    forRows(pool, n, [&](Index begin, Index end) {
        std::array<Corner, 8> c{};
        for (Index v = begin; v < end; ++v) {
            const std::size_t at = toSize(P.rowPtr[toSize(v)]);
            if (coarseOf[toSize(v)] >= 0) {
                P.col[at] = coarseOf[toSize(v)];
                P.val[at] = 1.0;
                continue;
            }
            if (role[toSize(v)] != 2) {
                continue;
            }
            const int m = corners(v, c);
            double sum = 0.0;
            for (int q = 0; q < m; ++q) {
                sum += c[toSize(q)].w;
            }
            // Insertion sort by coarse column (m ≤ 8, distinct keys). std::sort on the 8-entry
            // array triggers a GCC 13 -Warray-bounds false positive under -O2 + sanitizers.
            for (int q = 1; q < m; ++q) {
                const Corner key = c[toSize(q)];
                int p = q - 1;
                while (p >= 0 && coarseOf[toSize(c[toSize(p)].node)] > coarseOf[toSize(key.node)]) {
                    c[toSize(p + 1)] = c[toSize(p)];
                    --p;
                }
                c[toSize(p + 1)] = key;
            }
            for (int q = 0; q < m; ++q) {
                P.col[at + toSize(q)] = coarseOf[toSize(c[toSize(q)].node)];
                P.val[at + toSize(q)] = c[toSize(q)].w / sum;
            }
        }
    });
    return P;
}

// ---- Hierarchy --------------------------------------------------------------------------------

Result<SetupReport> AmgHierarchy::build(const CsrMatrix& K, sparse::KernelPool& pool,
                                        const SolverOptions& options, const CoordinateHint& hint) {
    if (K.rows != K.cols) {
        return makeError(ErrorCode::InvalidInput,
                         "AMG needs a square matrix, got " + std::to_string(K.rows) + " × " +
                             std::to_string(K.cols) + ".",
                         "This is a programming error in the caller.");
    }
    for (const double v : K.val) {
        if (!std::isfinite(v)) {
            return makeError(ErrorCode::NonFinite, "The matrix contains NaN or infinite entries.",
                             "Check the assembly inputs.");
        }
    }
    const auto t0 = Clock::now();
    pool_ = &pool;
    sweeps_ = std::max(1, options.amgSweeps);
    ops_.assign(1, &K);
    owned_.clear();
    levels_.clear();
    coarse_.reset();
    single_ = options.amgSinglePrecision;
    smoother_ = options.amgSmoother;
    chebDegree_ = std::max(1, options.amgChebyshevDegree);
    chebRatio_ = options.amgChebyshevRatio > 1.0 ? options.amgChebyshevRatio : 30.0;
    chebPowerIterations_ = std::max(1, options.amgChebyshevPowerIterations);
    fl_.clear();
    SetupReport report;

    // Geometric coarsening: grid lines from the hint (V3d).
    const bool geometric = options.coarsening == Coarsening::Geometric;
    std::vector<std::array<std::int32_t, 3>> levelGrid;
    std::array<std::vector<double>, 3> lines;
    if (geometric) {
        if (hint.grid == nullptr || hint.xyz == nullptr || hint.grid->size() != toSize(K.rows) ||
            hint.xyz->size() != toSize(K.rows)) {
            report.status = SolveStatus::Breakdown;
            report.detail = "Geometric multigrid needs the grid index and coordinates of every "
                            "node (CoordinateHint); none were given for this matrix.";
            report.seconds = secondsSince(t0);
            return report;
        }
        levelGrid = *hint.grid;
        std::array<std::int32_t, 3> top{};
        for (const auto& g : levelGrid) {
            for (std::size_t a = 0; a < 3; ++a) {
                top[a] = std::max(top[a], g[a]);
            }
        }
        const double points = (top[0] + 1.0) * (top[1] + 1.0) * (top[2] + 1.0);
        if (points > 4.0e8) {
            report.status = SolveStatus::OverBudget;
            report.detail = "The node grid has too many points for geometric coarsening.";
            report.seconds = secondsSince(t0);
            return report;
        }
        for (std::size_t a = 0; a < 3; ++a) {
            lines[a].assign(toSize(top[a]) + 1, std::numeric_limits<double>::quiet_NaN());
        }
        for (std::size_t v = 0; v < levelGrid.size(); ++v) {
            for (std::size_t a = 0; a < 3; ++a) {
                lines[a][toSize(levelGrid[v][a])] = (*hint.xyz)[v][a];
            }
        }
        for (std::size_t a = 0; a < 3;
             ++a) { // a line without nodes: index-linear fill (not expected)
            auto& x = lines[a];
            for (std::size_t i = 0; i < x.size(); ++i) {
                if (std::isnan(x[i])) {
                    x[i] = i > 0 ? x[i - 1] + (i > 1 ? x[i - 1] - x[i - 2] : 1e-3) : 0.0;
                }
            }
        }
    }

    const auto tc = Clock::now();
    // Time of each coarsening step summed over the levels (V3c profile).
    double tStrength = 0.0;
    double tAggregate = 0.0;
    double tProlong = 0.0;
    double tGalerkin = 0.0;
    double tSymmetrise = 0.0;
    while (ops_.back()->rows > options.amgCoarseSize &&
           static_cast<int>(ops_.size()) < options.amgMaxLevels) {
        const CsrMatrix& A = *ops_.back();
        Index count = 0;
        Level lv;
        auto ts = Clock::now();
        if (geometric) {
            const auto stride = static_cast<std::int32_t>(1) << ops_.size(); // 2, 4, 8, …
            std::vector<std::array<std::int32_t, 3>> coarseGrid;
            lv.P = geometricProlongator(A, levelGrid, stride, lines, coarseGrid, pool_);
            count = lv.P.cols;
            if (count == 0 || static_cast<double>(count) > 0.9 * static_cast<double>(A.rows)) {
                break; // coarsening stalled
            }
            levelGrid = std::move(coarseGrid);
        } else {
            const double theta =
                options.amgStrengthThreshold *
                std::pow(options.amgStrengthDecay, static_cast<double>(ops_.size() - 1));
            const std::vector<char> strong = strongConnections(A, theta, pool_);
            tStrength += secondsSince(ts);
            ts = Clock::now();
            lv.aggregateOf = aggregate(A, strong, count);
            tAggregate += secondsSince(ts);
            if (count == 0 || static_cast<double>(count) > 0.9 * static_cast<double>(A.rows)) {
                break; // coarsening stalled
            }
            ts = Clock::now();
            lv.P =
                smoothedProlongator(A, strong, tentativeProlongator(lv.aggregateOf, count), pool_);
        }
        lv.R = sparse::transpose(lv.P);
        tProlong += secondsSince(ts);
        ts = Clock::now();
        auto AP = sparse::multiply(*pool_, A, lv.P);
        if (!AP) {
            return std::move(AP).error();
        }
        auto Ac = sparse::multiply(*pool_, lv.R, AP.value());
        if (!Ac) {
            return std::move(Ac).error();
        }
        tGalerkin += secondsSince(ts);
        ts = Clock::now();
        lv.r.assign(toSize(A.rows), 0.0);
        lv.tmp.assign(toSize(A.rows), 0.0);
        lv.bc.assign(toSize(count), 0.0);
        lv.xc.assign(toSize(count), 0.0);
        // Pᵀ(A·P) is symmetric in exact arithmetic; rounding in the two products leaves tiny
        // asymmetries on entries that cancel. Averaging with the transpose (same pattern) makes
        // every coarse operator, and so the V-cycle, exactly symmetric (amendment A15).
        CsrMatrix coarse = std::move(Ac).value();
        const CsrMatrix coarseT = sparse::transpose(coarse);
        if (coarseT.col == coarse.col) {
            for (std::size_t q = 0; q < coarse.val.size(); ++q) {
                coarse.val[q] = 0.5 * (coarse.val[q] + coarseT.val[q]);
            }
        }
        owned_.push_back(std::make_unique<CsrMatrix>(std::move(coarse)));
        ops_.push_back(owned_.back().get());
        levels_.push_back(std::move(lv));
        tSymmetrise += secondsSince(ts);
    }
    report.phases.emplace_back("coarsening", secondsSince(tc));
    report.phases.emplace_back("  strength", tStrength);
    report.phases.emplace_back("  aggregation", tAggregate);
    report.phases.emplace_back("  prolongator", tProlong);
    report.phases.emplace_back("  Galerkin product", tGalerkin);
    report.phases.emplace_back("  symmetrise", tSymmetrise);

    const auto td = Clock::now();
    coarse_ = std::make_unique<DirectCholesky>(SolverKind::DirectRCM, Ordering::Rcm);
    auto cs = coarse_->setup(*ops_.back(), {}, pool, {});
    if (!cs) {
        return std::move(cs).error();
    }
    report.phases.emplace_back("coarse factorisation", secondsSince(td));
    if (cs.value().status != SolveStatus::Converged) {
        report.status = cs.value().status;
        report.detail = "Coarsest-level factorisation failed: " + cs.value().detail;
        report.seconds = secondsSince(t0);
        return report;
    }
    coarseSolution_.assign(toSize(ops_.back()->rows), 0.0);
    if (smoother_ == AmgSmoother::Chebyshev) {
        const auto te = Clock::now();
        buildChebyshev();
        report.phases.emplace_back("Chebyshev eigenvalue estimates", secondsSince(te));
    }
    if (single_) {
        const auto ts = Clock::now();
        buildSinglePrecision();
        report.phases.emplace_back("single-precision copy", secondsSince(ts));
    }

    // Statistics and work per cycle.
    const double nnz0 = static_cast<double>(K.nnz());
    const double rows0 = static_cast<double>(K.rows);
    for (const CsrMatrix* A : ops_) {
        report.levels.emplace_back(A->rows, A->nnz());
        report.operatorComplexity += static_cast<double>(A->nnz()) / nnz0;
        report.gridComplexity += static_cast<double>(A->rows) / rows0;
    }
    bytesPerCycle_ = 0.0;
    const auto pass = single_ ? passBytesSingle : passBytes;
    for (std::size_t l = 0; l < levels_.size(); ++l) {
        const double smoothing =
            smoother_ == AmgSmoother::Chebyshev
                ? 2.0 * chebyshevBytes(*ops_[l], chebDegree_, single_ ? 4.0 : 8.0)
                : 2.0 * sweeps_ * pass(*ops_[l]);
        bytesPerCycle_ += smoothing + pass(*ops_[l]) + pass(levels_[l].R) + pass(levels_[l].P);
    }
    if (single_) { // b and x converted to float and back on level 0
        bytesPerCycle_ += 24.0 * static_cast<double>(K.rows);
    }
    bytesPerCycle_ += 2.0 * 12.0 * static_cast<double>(coarse_->factor().val.size());
    report.footprint = footprint();
    report.seconds = secondsSince(t0);
    return report;
}

void AmgHierarchy::buildSinglePrecision() {
    const std::size_t L = ops_.size();
    fl_.assign(L, FloatLevel{});
    const auto view = [&](const CsrMatrix& A) {
        FloatCsr f;
        f.rows = A.rows;
        f.cols = A.cols;
        f.rowPtr = &A.rowPtr;
        f.col = &A.col;
        f.val = toFloat(A.val, *pool_);
        return f;
    };
    for (std::size_t l = 0; l < L; ++l) {
        FloatLevel& f = fl_[l];
        f.A = view(*ops_[l]);
        f.b.assign(toSize(ops_[l]->rows), 0.0f);
        f.x.assign(toSize(ops_[l]->rows), 0.0f);
        if (l + 1 < L) {
            f.P = view(levels_[l].P);
            f.R = view(levels_[l].R);
            f.r.assign(toSize(ops_[l]->rows), 0.0f);
            f.tmp.assign(toSize(ops_[l]->rows), 0.0f);
            // The double work vectors and transfer values are not used by the float cycle.
            Level& lv = levels_[l];
            if (smoother_ == AmgSmoother::Chebyshev) {
                f.invDiag = toFloat(lv.invDiag, *pool_);
                f.d.assign(toSize(ops_[l]->rows), 0.0f);
            }
            for (Vector* v : {&lv.r, &lv.tmp, &lv.bc, &lv.xc, &lv.invDiag, &lv.d}) {
                v->clear();
                v->shrink_to_fit();
            }
            releaseValues(lv.P);
            releaseValues(lv.R);
        }
    }
    // Coarse operators between level 0 (K, not owned) and the coarsest (kept for its factor).
    for (std::size_t l = 1; l + 1 < L; ++l) {
        releaseValues(*owned_[l - 1]);
    }
    coarseRhs_.assign(toSize(ops_.back()->rows), 0.0);
}

void AmgHierarchy::vcycle(const Vector& b, Vector& x) {
    if (!single_) {
        cycle(0, b, x);
        return;
    }
    FloatLevel& f0 = fl_[0];
    for (std::size_t i = 0; i < b.size(); ++i) {
        f0.b[i] = static_cast<float>(b[i]);
        f0.x[i] = static_cast<float>(x[i]);
    }
    cycleSingle(0);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = static_cast<double>(f0.x[i]);
    }
}

void AmgHierarchy::cycleSingle(std::size_t level) {
    FloatLevel& f = fl_[level];
    if (level + 1 == fl_.size()) {
        for (std::size_t i = 0; i < f.b.size(); ++i) {
            coarseRhs_[i] = static_cast<double>(f.b[i]);
        }
        auto r = coarse_->solve(coarseRhs_, coarseSolution_, {});
        (void)r; // sizes match by construction; the factor exists
        for (std::size_t i = 0; i < f.x.size(); ++i) {
            f.x[i] = static_cast<float>(coarseSolution_[i]);
        }
        return;
    }
    FloatLevel& c = fl_[level + 1];
    const auto smooth = [&](bool forward) {
        if (smoother_ == AmgSmoother::Chebyshev) {
            chebyshev<float>(*pool_, f.A.rows, f.A.rowPtr->data(), f.A.col->data(), f.A.val.data(),
                             f.invDiag.data(), f.b.data(), f.x.data(), f.r.data(), f.d.data(),
                             levels_[level].lambdaMax, chebRatio_, chebDegree_);
            return;
        }
        for (int s = 0; s < sweeps_; ++s) {
            gaussSeidelSingle(f.A, f.b, f.x, forward);
        }
    };
    smooth(true);
    spmvSingle(*pool_, f.A, f.x, f.r, &f.b);
    spmvSingle(*pool_, f.R, f.r, c.b);
    std::fill(c.x.begin(), c.x.end(), 0.0f);
    cycleSingle(level + 1);
    spmvSingle(*pool_, f.P, c.x, f.tmp);
    pool_->parallelFor(f.A.rows, [&](Index begin, Index end) {
        for (Index i = begin; i < end; ++i) {
            f.x[toSize(i)] += f.tmp[toSize(i)];
        }
    });
    smooth(false);
}

void AmgHierarchy::vcycleBlock(const Vector& B, Vector& X, int k) {
    const std::size_t n = toSize(ops_[0]->rows);
    const std::size_t kk = toSize(k);
    if (!single_ || smoother_ != AmgSmoother::Chebyshev) { // no block kernels: k cycles
        Vector b(n), x(n);
        for (std::size_t c = 0; c < kk; ++c) {
            for (std::size_t i = 0; i < n; ++i) {
                b[i] = B[i * kk + c];
                x[i] = 0.0;
            }
            vcycle(b, x);
            for (std::size_t i = 0; i < n; ++i) {
                X[i * kk + c] = x[i];
            }
        }
        return;
    }
    for (FloatLevel& f : fl_) {
        const std::size_t m = f.b.size() * kk;
        if (f.bB.size() != m) {
            f.bB.assign(m, 0.0f);
            f.xB.assign(m, 0.0f);
            if (!f.r.empty()) {
                f.rB.assign(m, 0.0f);
                f.tmpB.assign(m, 0.0f);
                f.dB.assign(m, 0.0f);
            }
        }
    }
    FloatLevel& f0 = fl_[0];
    pool_->parallelFor(static_cast<Index>(n * kk), [&](Index begin, Index end) {
        for (Index i = begin; i < end; ++i) {
            f0.bB[toSize(i)] = static_cast<float>(B[toSize(i)]);
            f0.xB[toSize(i)] = 0.0f;
        }
    });
    cycleSingleBlock(0, k);
    pool_->parallelFor(static_cast<Index>(n * kk), [&](Index begin, Index end) {
        for (Index i = begin; i < end; ++i) {
            X[toSize(i)] = static_cast<double>(f0.xB[toSize(i)]);
        }
    });
}

void AmgHierarchy::cycleSingleBlock(std::size_t level, int k) {
    FloatLevel& f = fl_[level];
    const std::size_t kk = toSize(k);
    if (level + 1 == fl_.size()) {
        const std::size_t n = f.b.size();
        for (std::size_t c = 0; c < kk; ++c) {
            for (std::size_t i = 0; i < n; ++i) {
                coarseRhs_[i] = static_cast<double>(f.bB[i * kk + c]);
            }
            auto r = coarse_->solve(coarseRhs_, coarseSolution_, {});
            (void)r; // sizes match by construction; the factor exists
            for (std::size_t i = 0; i < n; ++i) {
                f.xB[i * kk + c] = static_cast<float>(coarseSolution_[i]);
            }
        }
        return;
    }
    FloatLevel& c = fl_[level + 1];
    const auto smooth = [&] {
        chebyshevBlock<float>(*pool_, f.A.rows, f.A.rowPtr->data(), f.A.col->data(), f.A.val.data(),
                              f.invDiag.data(), f.bB.data(), f.xB.data(), f.rB.data(), f.dB.data(),
                              k, levels_[level].lambdaMax, chebRatio_, chebDegree_);
    };
    smooth();
    blockSpmv(*pool_, f.A.rows, f.A.rowPtr->data(), f.A.col->data(), f.A.val.data(), f.xB.data(),
              f.rB.data(), k, f.bB.data());
    blockSpmv(*pool_, f.R.rows, f.R.rowPtr->data(), f.R.col->data(), f.R.val.data(), f.rB.data(),
              c.bB.data(), k);
    std::fill(c.xB.begin(), c.xB.end(), 0.0f);
    cycleSingleBlock(level + 1, k);
    blockSpmv(*pool_, f.P.rows, f.P.rowPtr->data(), f.P.col->data(), f.P.val.data(), c.xB.data(),
              f.tmpB.data(), k);
    pool_->parallelFor(static_cast<Index>(f.xB.size()), [&](Index begin, Index end) {
        for (Index i = begin; i < end; ++i) {
            f.xB[toSize(i)] += f.tmpB[toSize(i)];
        }
    });
    smooth();
}

std::size_t AmgHierarchy::blockBytes() const noexcept {
    std::size_t bytes = 0;
    for (const FloatLevel& f : fl_) {
        bytes += (f.bB.capacity() + f.xB.capacity() + f.rB.capacity() + f.tmpB.capacity() +
                  f.dB.capacity()) *
                 sizeof(float);
    }
    return bytes;
}

void AmgHierarchy::cycle(std::size_t level, const Vector& b, Vector& x) {
    if (level + 1 == ops_.size()) {
        auto r = coarse_->solve(b, coarseSolution_, {});
        (void)r; // sizes match by construction; the factor exists
        std::copy(coarseSolution_.begin(), coarseSolution_.end(), x.begin());
        return;
    }
    const CsrMatrix& A = *ops_[level];
    Level& lv = levels_[level];
    const auto smooth = [&](bool forward) {
        if (smoother_ == AmgSmoother::Chebyshev) {
            chebyshev<double>(*pool_, A.rows, A.rowPtr.data(), A.col.data(), A.val.data(),
                              lv.invDiag.data(), b.data(), x.data(), lv.r.data(), lv.d.data(),
                              lv.lambdaMax, chebRatio_, chebDegree_);
            return;
        }
        for (int s = 0; s < sweeps_; ++s) {
            gaussSeidel(A, b, x, forward);
        }
    };
    smooth(true);
    sparse::residual(*pool_, A, x, b, lv.r);
    sparse::spmv(*pool_, lv.R, lv.r, lv.bc);
    std::fill(lv.xc.begin(), lv.xc.end(), 0.0);
    cycle(level + 1, lv.bc, lv.xc);
    sparse::spmv(*pool_, lv.P, lv.xc, lv.tmp);
    sparse::axpy(*pool_, 1.0, lv.tmp, x);
    smooth(false);
}

void AmgHierarchy::buildChebyshev() {
    // λ̂ = 1.1 × the Rayleigh quotient vᵀAv / vᵀDv after 20 power iterations with D⁻¹A, from a
    // fixed pseudo-random start (deterministic, and not near the smooth near-null space). Every
    // loop runs on the pool; sums use the pool's chunk-ordered reduction (bit-identical for any
    // thread count). The start entry is a hash of the row index, so it needs no sequential state.
    sparse::KernelPool& pool = *pool_;
    for (std::size_t l = 0; l < levels_.size(); ++l) {
        const CsrMatrix& A = *ops_[l];
        Level& lv = levels_[l];
        const std::size_t n = toSize(A.rows);
        Vector diag(n, 0.0);
        lv.invDiag.assign(n, 0.0);
        lv.d.assign(n, 0.0);
        Vector v(n);
        Vector w(n, 0.0);
        pool.parallelFor(A.rows, [&](Index begin, Index end) {
            for (Index i = begin; i < end; ++i) {
                const std::size_t k = toSize(i);
                diag[k] = A.at(i, i);
                lv.invDiag[k] = diag[k] != 0.0 ? 1.0 / diag[k] : 0.0;
                std::uint64_t h = (static_cast<std::uint64_t>(i) + 1) * 0x9E3779B97F4A7C15ull;
                h ^= h >> 31;
                h *= 0xBF58476D1CE4E5B9ull;
                h ^= h >> 29;
                v[k] = static_cast<double>(h % 2001u) / 1000.0 - 1.0;
            }
        });
        double lambda = 0.0;
        for (int it = 0; it < chebPowerIterations_; ++it) {
            sparse::spmv(pool, A, v, w);
            const double vAv = pool.reduceSum(A.rows, [&](Index begin, Index end) {
                double s = 0.0;
                for (Index i = begin; i < end; ++i) {
                    s += v[toSize(i)] * w[toSize(i)];
                }
                return s;
            });
            const double vDv = pool.reduceSum(A.rows, [&](Index begin, Index end) {
                double s = 0.0;
                for (Index i = begin; i < end; ++i) {
                    s += v[toSize(i)] * v[toSize(i)] * diag[toSize(i)];
                }
                return s;
            });
            // w ← D⁻¹w and ‖w‖² in one pass (each row is written once).
            double norm = pool.reduceSum(A.rows, [&](Index begin, Index end) {
                double s = 0.0;
                for (Index i = begin; i < end; ++i) {
                    w[toSize(i)] *= lv.invDiag[toSize(i)];
                    s += w[toSize(i)] * w[toSize(i)];
                }
                return s;
            });
            lambda = vDv > 0.0 ? vAv / vDv : 0.0;
            norm = std::sqrt(norm);
            if (!(norm > 0.0)) {
                break;
            }
            pool.parallelFor(A.rows, [&](Index begin, Index end) {
                for (Index i = begin; i < end; ++i) {
                    v[toSize(i)] = w[toSize(i)] / norm;
                }
            });
        }
        lv.lambdaMax = 1.1 * (lambda > 0.0 ? lambda : 2.0);
    }
}

MemoryFootprint AmgHierarchy::footprint() const {
    MemoryFootprint f;
    std::size_t ops = 0;
    for (const auto& m : owned_) {
        ops += m->bytes();
    }
    f.add("AMG coarse operators", ops);
    std::size_t transfer = 0;
    std::size_t work = 0;
    for (const Level& lv : levels_) {
        transfer += lv.P.bytes() + lv.R.bytes();
        work += (lv.r.capacity() + lv.tmp.capacity() + lv.bc.capacity() + lv.xc.capacity() +
                 lv.invDiag.capacity() + lv.d.capacity()) *
                    sizeof(double) +
                lv.aggregateOf.capacity() * sizeof(Index);
    }
    f.add("AMG prolongators and restrictions", transfer);
    f.add("AMG work vectors and aggregates", work);
    if (single_) {
        std::size_t values = 0;
        std::size_t vectors = 0;
        for (const FloatLevel& lv : fl_) {
            values +=
                (lv.A.val.capacity() + lv.P.val.capacity() + lv.R.val.capacity()) * sizeof(float);
            vectors += (lv.b.capacity() + lv.x.capacity() + lv.r.capacity() + lv.tmp.capacity() +
                        lv.invDiag.capacity() + lv.d.capacity()) *
                       sizeof(float);
        }
        f.add("AMG single-precision values (level 0 included)", values);
        f.add("AMG single-precision work vectors",
              vectors + coarseRhs_.capacity() * sizeof(double));
    }
    if (coarse_) {
        f.add("AMG coarse factor", coarse_->footprint().total());
    }
    return f;
}

// ---- AMG as preconditioner --------------------------------------------------------------------

SetupReport AmgPreconditioner::setup(const CsrMatrix& K, sparse::KernelPool& pool) {
    auto r = h_.build(K, pool, options_, hint_);
    if (!r) {
        SetupReport fail;
        fail.status = SolveStatus::Breakdown;
        fail.detail = r.error().message;
        return fail;
    }
    return r.value();
}

void AmgPreconditioner::apply(const Vector& r, Vector& z) {
    std::fill(z.begin(), z.end(), 0.0);
    h_.vcycle(r, z);
}

// ---- AMG as stand-alone solver ----------------------------------------------------------------

Result<SetupReport> AmgSolver::setup(const CsrMatrix& K, const CoordinateHint& /*hint*/,
                                     sparse::KernelPool& pool, const SolverOptions& options) {
    ready_ = false;
    SolverOptions doubleOnly = options; // the stand-alone iterate needs double precision
    doubleOnly.amgSinglePrecision = false;
    auto r = h_.build(K, pool, doubleOnly);
    if (!r) {
        return std::move(r).error();
    }
    SetupReport report = std::move(r).value();
    if (report.status != SolveStatus::Converged) {
        return report;
    }
    if (report.footprint.total() > options.memoryBudgetBytes) {
        report.status = SolveStatus::OverBudget;
        report.detail = "The AMG hierarchy needs " + std::to_string(report.footprint.total()) +
                        " bytes, more than the budget of " +
                        std::to_string(options.memoryBudgetBytes) + ".";
        return report;
    }
    k_ = &K;
    pool_ = &pool;
    r_.assign(toSize(K.rows), 0.0);
    ready_ = true;
    report.footprint = footprint();
    return report;
}

MemoryFootprint AmgSolver::footprint() const {
    MemoryFootprint f = h_.footprint();
    f.add("AMG solver residual", r_.capacity() * sizeof(double));
    return f;
}

Result<SolveReport> AmgSolver::solve(const Vector& f, Vector& x, const SolveControl& control) {
    if (!ready_) {
        return makeError(ErrorCode::InvalidInput, "AMG was not set up successfully.",
                         "Call setup() and check its status first.");
    }
    const CsrMatrix& K = *k_;
    sparse::KernelPool& pool = *pool_;
    const std::size_t n = toSize(K.rows);
    if (f.size() != n || x.size() != n ||
        (control.residualSumMask != nullptr && control.residualSumMask->size() != n)) {
        return makeError(ErrorCode::InvalidInput,
                         "Right-hand side, initial guess or mask does not match the " +
                             std::to_string(n) + " rows of K.",
                         "This is a programming error in the caller.");
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(f[i]) || !std::isfinite(x[i])) {
            return makeError(ErrorCode::NonFinite,
                             "The right-hand side or the initial guess contains NaN or infinity.",
                             "Check the load and boundary-condition inputs.");
        }
    }
    const double spmvBytes = passBytes(K);
    SolveReport rep;
    double observerSeconds = 0.0;
    const auto t0 = Clock::now();
    const auto solverSeconds = [&] { return secondsSince(t0) - observerSeconds; };
    double bytes = 0.0;

    const double normF = sparse::norm2(pool, f);
    if (normF == 0.0) {
        std::fill(x.begin(), x.end(), 0.0);
        rep.residualHistory.push_back(0.0);
    } else {
        sparse::residual(pool, K, x, f, r_);
        ++rep.spmvCount;
        bytes += spmvBytes;
        double rel = sparse::norm2(pool, r_) / normF;
        rep.residualHistory.push_back(rel);
        int growing = 0;
        double best = rel;
        int windowStart = 0;
        double windowBest = rel;
        int k = 0;
        while (rel > control.relTol) {
            if (k >= control.maxIterations) {
                rep.status = SolveStatus::MaxIterations;
                rep.detail = "Reached the cap of " + std::to_string(control.maxIterations) +
                             " cycles at relative residual " + number(rel) + ".";
                break;
            }
            ++k;
            h_.vcycle(f, x);
            sparse::residual(pool, K, x, f, r_);
            ++rep.spmvCount;
            bytes += h_.bytesPerCycle() + spmvBytes;
            const double next = sparse::norm2(pool, r_) / normF;
            rep.residualHistory.push_back(next);
            if (control.observer != nullptr) {
                const auto o0 = Clock::now();
                control.observer->onIteration(k, next, x);
                observerSeconds += secondsSince(o0);
            }
            rep.iterTimestamps.push_back(solverSeconds());
            if (!std::isfinite(next)) {
                rep.status = SolveStatus::Breakdown;
                rep.detail =
                    "The residual became NaN or infinite at cycle " + std::to_string(k) + ".";
                break;
            }
            growing = next > rel ? growing + 1 : 0;
            rel = next;
            if (rel <= control.relTol) {
                break;
            }
            best = std::min(best, rel);
            // Real divergence leaves the best residual far behind; growth near the rounding floor
            // (seen on HS-7 at ~3e-12) does not.
            if (growing >= 3 && rel > 10.0 * best) {
                rep.status = SolveStatus::Diverging;
                rep.detail = "The residual grew for three cycles in a row to more than 10× its "
                             "best (relative residual " +
                             number(rel) + " at cycle " + std::to_string(k) + ").";
                break;
            }
            if (rel < windowBest / 10.0) {
                windowBest = rel;
                windowStart = k;
            } else if (control.stagnationWindow > 0 &&
                       k - windowStart >= control.stagnationWindow) {
                rep.status = SolveStatus::Stagnated;
                rep.detail = "The residual did not drop 10× within " +
                             std::to_string(control.stagnationWindow) + " cycles.";
                break;
            }
            if (rep.iterTimestamps.back() > control.wallTimeCap) {
                rep.status = SolveStatus::TimeCap;
                rep.detail = "Reached the wall-time cap of " + number(control.wallTimeCap) +
                             " s after " + std::to_string(k) + " cycles.";
                break;
            }
            if (control.cancel != nullptr && control.cancel->cancelled()) {
                rep.status = SolveStatus::Cancelled;
                rep.detail = "Cancelled after " + std::to_string(k) + " cycles.";
                break;
            }
        }
        rep.iterations = k;
    }
    rep.seconds = solverSeconds();
    rep.observerSeconds = observerSeconds;
    rep.bytesMoved = bytes;
    rep.workSpmvEq = spmvBytes > 0.0 ? bytes / spmvBytes : 0.0;

    sparse::residual(pool, K, x, f, r_);
    rep.finalTrueRelResidual = normF > 0.0 ? sparse::norm2(pool, r_) / normF : 0.0;
    if (control.residualSumMask != nullptr) {
        double s = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            if ((*control.residualSumMask)[i] != 0) {
                s += r_[i];
            }
        }
        rep.residualSum = s;
    } else {
        rep.residualSum = sparse::sum(pool, r_);
    }
    return rep;
}

// ---- AMG-PCG-CH-SP for several load cases (V3e) ----------------------------------------------

Result<SetupReport> AmgBatchPcg::setup(const CsrMatrix& K, const CoordinateHint& /*hint*/,
                                       sparse::KernelPool& pool, const SolverOptions& options) {
    ready_ = false;
    SolverOptions o = options;
    o.amgSinglePrecision = true;
    o.amgSmoother = AmgSmoother::Chebyshev;
    o.coarsening = Coarsening::Aggregation;
    auto r = h_.build(K, pool, o);
    if (!r) {
        return std::move(r).error();
    }
    SetupReport report = std::move(r).value();
    if (report.status != SolveStatus::Converged) {
        return report;
    }
    k_ = &K;
    pool_ = &pool;
    budget_ = options.memoryBudgetBytes;
    ready_ = true;
    report.footprint = footprint();
    return report;
}

Result<SolveReport> AmgBatchPcg::solve(const Vector& f, Vector& x, const SolveControl& control) {
    const std::vector<Vector> F{f};
    std::vector<Vector> X{x};
    auto r = solveMany(F, X, control);
    if (r) {
        x = std::move(X[0]);
    }
    return r;
}

Result<SolveReport> AmgBatchPcg::solveMany(const std::vector<Vector>& F, std::vector<Vector>& X,
                                           const SolveControl& control) {
    if (!ready_) {
        return makeError(ErrorCode::InvalidInput, "Batch PCG was not set up successfully.",
                         "Call setup() and check its status first.");
    }
    const std::size_t n = toSize(k_->rows);
    const int k = static_cast<int>(F.size());
    if (k < 1 || k > AmgHierarchy::kMaxBlock || X.size() != F.size()) {
        return makeError(ErrorCode::InvalidInput,
                         "Batch PCG takes 1 to " + std::to_string(AmgHierarchy::kMaxBlock) +
                             " load cases with one solution vector each.",
                         "This is a programming error in the caller.");
    }
    for (int c = 0; c < k; ++c) {
        if (F[toSize(c)].size() != n || X[toSize(c)].size() != n) {
            return makeError(ErrorCode::InvalidInput, "A load case does not match the rows of K.",
                             "This is a programming error in the caller.");
        }
    }
    SolveReport rep;
    // Block memory: 6 double vectors here plus about 5 float vectors per level (with the
    // coarse levels, about 1.2 times level 0) in the hierarchy.
    const double blockBytes =
        static_cast<double>(n) * static_cast<double>(k) * (6.0 * 8.0 + 5.0 * 4.0 * 1.2);
    if (blockBytes + static_cast<double>(h_.footprint().total()) > static_cast<double>(budget_)) {
        rep.status = SolveStatus::OverBudget;
        rep.detail =
            "The block vectors for " + std::to_string(k) + " load cases exceed the budget.";
        return rep;
    }
    const auto t0 = Clock::now();
    sparse::KernelPool& pool = *pool_;
    const CsrMatrix& K = *k_;
    const std::size_t kk = toSize(k);
    const std::size_t m = n * kk;
    const Index total = static_cast<Index>(m);
    for (Vector* v : {&R_, &Z_, &P_, &Q_, &Xb_, &Fb_}) {
        v->assign(m, 0.0);
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t c = 0; c < kk; ++c) {
            Fb_[i * kk + c] = F[c][i];
            Xb_[i * kk + c] = X[c][i];
        }
    }
    const auto norms = [&](const Vector& V) {
        std::vector<double> s = blockSums(pool, K.rows, k, [&](Index i, double* acc) {
            for (std::size_t c = 0; c < kk; ++c) {
                const double v = V[toSize(i) * kk + c];
                acc[c] += v * v;
            }
        });
        for (double& v : s) {
            v = std::sqrt(v);
        }
        return s;
    };
    const auto dots = [&](const Vector& A, const Vector& B) {
        return blockSums(pool, K.rows, k, [&](Index i, double* acc) {
            for (std::size_t c = 0; c < kk; ++c) {
                acc[c] += A[toSize(i) * kk + c] * B[toSize(i) * kk + c];
            }
        });
    };
    const std::vector<double> normF = norms(Fb_);
    blockSpmv(pool, K.rows, K.rowPtr.data(), K.col.data(), K.val.data(), Xb_.data(), R_.data(), k,
              Fb_.data());
    std::vector<double> rel = norms(R_);
    std::vector<char> active(kk, 1);
    double worst = 0.0;
    for (std::size_t c = 0; c < kk; ++c) {
        rel[c] = normF[c] > 0.0 ? rel[c] / normF[c] : 0.0;
        if (rel[c] <= control.relTol) {
            active[c] = 0;
        }
        worst = std::max(worst, rel[c]);
    }
    rep.residualHistory.push_back(worst);
    const auto anyActive = [&] {
        return std::any_of(active.begin(), active.end(), [](char a) { return a != 0; });
    };
    std::vector<double> rz(kk, 0.0);
    if (anyActive()) {
        h_.vcycleBlock(R_, Z_, k);
        rz = dots(R_, Z_);
        P_ = Z_;
    }
    std::vector<double> alpha(kk, 0.0);
    std::vector<double> beta(kk, 0.0);
    int it = 0;
    while (anyActive()) {
        if (it >= control.maxIterations) {
            rep.status = SolveStatus::MaxIterations;
            rep.detail =
                "Reached the cap of " + std::to_string(control.maxIterations) + " iterations.";
            break;
        }
        if (secondsSince(t0) > control.wallTimeCap) {
            rep.status = SolveStatus::TimeCap;
            break;
        }
        ++it;
        blockSpmv(pool, K.rows, K.rowPtr.data(), K.col.data(), K.val.data(), P_.data(), Q_.data(),
                  k);
        const std::vector<double> pq = dots(P_, Q_);
        for (std::size_t c = 0; c < kk; ++c) {
            if (active[c] != 0 && !(pq[c] > 0.0)) {
                rep.status = SolveStatus::Breakdown;
                rep.detail = "pTKp <= 0 for load case " + std::to_string(c) + ".";
            }
            alpha[c] = active[c] != 0 && pq[c] > 0.0 ? rz[c] / pq[c] : 0.0;
        }
        if (rep.status == SolveStatus::Breakdown) {
            break;
        }
        pool.parallelFor(total, [&](Index begin, Index end) {
            for (Index i = begin; i < end; ++i) {
                const double a = alpha[toSize(i) % kk];
                Xb_[toSize(i)] += a * P_[toSize(i)];
                R_[toSize(i)] -= a * Q_[toSize(i)];
            }
        });
        rel = norms(R_);
        worst = 0.0;
        for (std::size_t c = 0; c < kk; ++c) {
            rel[c] = normF[c] > 0.0 ? rel[c] / normF[c] : 0.0;
            if (active[c] != 0) {
                worst = std::max(worst, rel[c]);
                if (rel[c] <= control.relTol) {
                    active[c] = 0;
                }
            }
        }
        rep.residualHistory.push_back(worst);
        rep.iterTimestamps.push_back(secondsSince(t0));
        if (!anyActive()) {
            break;
        }
        h_.vcycleBlock(R_, Z_, k);
        const std::vector<double> rzNew = dots(R_, Z_);
        for (std::size_t c = 0; c < kk; ++c) {
            beta[c] = active[c] != 0 && rz[c] != 0.0 ? rzNew[c] / rz[c] : 0.0;
            rz[c] = rzNew[c];
        }
        pool.parallelFor(total, [&](Index begin, Index end) {
            for (Index i = begin; i < end; ++i) {
                const std::size_t c = toSize(i) % kk;
                if (active[c] != 0) {
                    P_[toSize(i)] = Z_[toSize(i)] + beta[c] * P_[toSize(i)];
                }
            }
        });
    }
    rep.iterations = it;
    rep.seconds = secondsSince(t0);
    // True residuals after the solve (not timed).
    blockSpmv(pool, K.rows, K.rowPtr.data(), K.col.data(), K.val.data(), Xb_.data(), Q_.data(), k,
              Fb_.data());
    const std::vector<double> trueNorm = norms(Q_);
    for (std::size_t c = 0; c < kk; ++c) {
        rep.finalTrueRelResidual = std::max(rep.finalTrueRelResidual,
                                            normF[c] > 0.0 ? trueNorm[c] / normF[c] : trueNorm[c]);
    }
    if (control.residualSumMask != nullptr) { // load case 0
        for (std::size_t i = 0; i < n; ++i) {
            if ((*control.residualSumMask)[i] != 0) {
                rep.residualSum += Q_[i * kk];
            }
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t c = 0; c < kk; ++c) {
            X[c][i] = Xb_[i * kk + c];
        }
    }
    return rep;
}

MemoryFootprint AmgBatchPcg::footprint() const {
    MemoryFootprint f = h_.footprint();
    f.add("AMG block work vectors (float)", h_.blockBytes());
    f.add("Batch PCG block vectors (R, Z, P, Q, X, F)",
          (R_.capacity() + Z_.capacity() + P_.capacity() + Q_.capacity() + Xb_.capacity() +
           Fb_.capacity()) *
              sizeof(double));
    return f;
}

} // namespace uniforge::solvers
