// Persistent kernel pool (spec §5.5): coverage, p-independent reductions, reuse, exceptions.

#include "core/sparse/KernelPool.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include "RandomSparse.hpp"

using namespace uniforge::sparse;
using uniforge::testing::randomVector;

namespace {

// Thread counts used throughout: serial, small, odd, more threads than this test's chunks.
const std::vector<int> kThreadCounts{1, 2, 3, 7, 20};

} // namespace

TEST(KernelPool, ChunkCount) {
    EXPECT_EQ(KernelPool::chunkCount(0), 0);
    EXPECT_EQ(KernelPool::chunkCount(-5), 0);
    EXPECT_EQ(KernelPool::chunkCount(1), 1);
    EXPECT_EQ(KernelPool::chunkCount(KernelPool::kChunkRows), 1);
    EXPECT_EQ(KernelPool::chunkCount(KernelPool::kChunkRows + 1), 2);
}

TEST(KernelPool, ThreadCountIsAtLeastOne) {
    EXPECT_EQ(KernelPool(0).threads(), 1);
    EXPECT_EQ(KernelPool(-3).threads(), 1);
    EXPECT_EQ(KernelPool(4).threads(), 4);
}

TEST(KernelPool, ParallelForVisitsEveryRowExactlyOnce) {
    const Index n = 10 * KernelPool::kChunkRows + 17; // last chunk is partial
    for (const int p : kThreadCounts) {
        KernelPool pool(p);
        std::vector<int> hits(static_cast<std::size_t>(n), 0);
        pool.parallelFor(n, [&](Index begin, Index end) {
            EXPECT_EQ(begin % KernelPool::kChunkRows, 0) << "chunks start on fixed boundaries";
            for (Index i = begin; i < end; ++i) {
                ++hits[static_cast<std::size_t>(i)];
            }
        });
        for (std::size_t i = 0; i < hits.size(); ++i) {
            ASSERT_EQ(hits[i], 1) << "p = " << p << ", row " << i;
        }
    }
}

TEST(KernelPool, ParallelForWithNothingToDo) {
    KernelPool pool(4);
    int calls = 0;
    pool.parallelFor(0, [&](Index, Index) { ++calls; });
    EXPECT_EQ(calls, 0);
}

TEST(KernelPool, ReduceSumIsBitIdenticalForEveryThreadCount) {
    const Index n = 37 * KernelPool::kChunkRows + 123;
    const std::vector<double> x = randomVector(n, 20261118);
    const auto partial = [&](Index begin, Index end) {
        double s = 0.0;
        for (Index i = begin; i < end; ++i) {
            s += x[static_cast<std::size_t>(i)];
        }
        return s;
    };

    // Reference: the same chunked sum written out serially.
    double expected = 0.0;
    for (Index c = 0; c < KernelPool::chunkCount(n); ++c) {
        const Index begin = c * KernelPool::kChunkRows;
        expected += partial(begin, std::min(n, begin + KernelPool::kChunkRows));
    }
    // Make sure the data is order-sensitive, otherwise the test proves nothing.
    double plain = 0.0;
    for (const double v : x) {
        plain += v;
    }
    ASSERT_NE(plain, expected) << "test data should make summation order visible";

    for (const int p : kThreadCounts) {
        KernelPool pool(p);
        for (int rep = 0; rep < 3; ++rep) {
            const double got = pool.reduceSum(n, partial);
            ASSERT_EQ(got, expected) << "p = " << p << ", repetition " << rep; // bitwise
        }
    }
}

TEST(KernelPool, ReduceMaxAndNaN) {
    const Index n = 3 * KernelPool::kChunkRows;
    std::vector<double> x(static_cast<std::size_t>(n), 1.0);
    x[5000] = 42.0;
    const auto partial = [&](Index begin, Index end) {
        double m = -std::numeric_limits<double>::infinity();
        for (Index i = begin; i < end; ++i) {
            m = std::max(m, x[static_cast<std::size_t>(i)]);
        }
        return m;
    };
    for (const int p : kThreadCounts) {
        KernelPool pool(p);
        EXPECT_EQ(pool.reduceMax(n, partial), 42.0);
        EXPECT_EQ(pool.reduceMax(0, partial), -std::numeric_limits<double>::infinity());
        EXPECT_TRUE(std::isnan(pool.reduceMax(n, [](Index begin, Index) {
            return begin == KernelPool::kChunkRows ? std::numeric_limits<double>::quiet_NaN() : 1.0;
        })));
    }
}

TEST(KernelPool, ManyShortCallsDoNotDeadlock) {
    KernelPool pool(8);
    const Index n = 2 * KernelPool::kChunkRows;
    double total = 0.0;
    for (int i = 0; i < 20000; ++i) {
        total += pool.reduceSum(n, [](Index begin, Index end) { return double(end - begin); });
    }
    EXPECT_EQ(total, 20000.0 * n);
}

TEST(KernelPool, UsesHelperThreadsAndCallsThreadStartOnce) {
    std::mutex m;
    std::multiset<int> started;
    {
        KernelPool pool(4, [&](int index) {
            const std::lock_guard<std::mutex> lock(m);
            started.insert(index);
        });
        std::atomic<int> chunks{0};
        pool.parallelFor(8 * KernelPool::kChunkRows, [&](Index, Index) { ++chunks; });
        EXPECT_EQ(chunks.load(), 8);
    } // destructor joins
    EXPECT_EQ(started, (std::multiset<int>{1, 2, 3}));
}

TEST(KernelPool, ExceptionInABodyIsRethrownAfterAllThreadsFinish) {
    KernelPool pool(4);
    const Index n = 16 * KernelPool::kChunkRows;
    std::atomic<int> chunks{0};
    EXPECT_THROW(pool.parallelFor(n,
                                  [&](Index begin, Index) {
                                      ++chunks;
                                      if (begin == 5 * KernelPool::kChunkRows) {
                                          throw std::runtime_error("boom");
                                      }
                                  }),
                 std::runtime_error);
    EXPECT_EQ(chunks.load(), 16) << "the other chunks still ran";
    // The pool is still usable afterwards.
    EXPECT_EQ(pool.reduceSum(n, [](Index b, Index e) { return double(e - b); }), double(n));
}
