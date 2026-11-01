// Bounded worker pool (spec §6): chunked scheduling, per-index writes, errors, exceptions,
// cancellation.

#include "core/perf/WorkerPool.hpp"

#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using uniforge::CancelToken;
using uniforge::ErrorCode;
using uniforge::makeError;
using uniforge::Result;
using uniforge::perf::WorkerPool;

TEST(WorkerPool, ChunkSizeFollowsSpec) {
    EXPECT_EQ(WorkerPool::chunkSize(4000, 4), 125U); // 4000 / 32
    EXPECT_EQ(WorkerPool::chunkSize(500, 2), 31U);
    EXPECT_EQ(WorkerPool::chunkSize(3, 4), 1U); // never 0
    EXPECT_EQ(WorkerPool::chunkSize(10, 0), 1U);
}

TEST(WorkerPool, VisitsEveryIndexExactlyOnce) {
    const std::size_t n = 10007; // prime: the last chunk is partial
    std::vector<int> hits(n, 0);
    const WorkerPool pool(4);
    const auto r = pool.run(n, [&](std::size_t i, int) -> Result<void> {
        ++hits[i]; // each index belongs to exactly one worker: no data race
        return {};
    });
    ASSERT_TRUE(r.ok());
    for (std::size_t i = 0; i < n; ++i) {
        ASSERT_EQ(hits[i], 1) << "index " << i;
    }
}

TEST(WorkerPool, UsesUpToPWorkersWithValidIds) {
    std::vector<int> worker(400, -1);
    const WorkerPool pool(4);
    ASSERT_TRUE(pool.run(worker.size(), [&](std::size_t i, int w) -> Result<void> {
                        worker[i] = w;
                        std::this_thread::yield();
                        return {};
                    }).ok());
    for (const int w : worker) {
        ASSERT_GE(w, 0);
        ASSERT_LT(w, 4);
    }
}

TEST(WorkerPool, MoreWorkersThanItemsStartsOnlyNeededThreads) {
    const WorkerPool pool(16);
    EXPECT_EQ(pool.threadsFor(3), 3);
    EXPECT_EQ(pool.threadsFor(100), 16);
    std::vector<int> hits(3, 0);
    std::vector<int> ids(3, -1);
    ASSERT_TRUE(pool.run(3, [&](std::size_t i, int w) -> Result<void> {
                        ++hits[i];
                        ids[i] = w;
                        return {};
                    }).ok());
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(hits[i], 1);
        EXPECT_LT(ids[i], 3);
    }
}

TEST(WorkerPool, ZeroOrNegativeWorkersMeansOne) {
    EXPECT_EQ(WorkerPool(0).workers(), 1);
    EXPECT_EQ(WorkerPool(-3).workers(), 1);
}

TEST(WorkerPool, EmptyRangeSucceeds) {
    const WorkerPool pool(4);
    EXPECT_TRUE(pool.run(0, [](std::size_t, int) -> Result<void> { return {}; }).ok());
}

TEST(WorkerPool, BodyErrorIsReturned) {
    const WorkerPool pool(4);
    const auto r = pool.run(1000, [](std::size_t i, int) -> Result<void> {
        if (i == 517) {
            return makeError(ErrorCode::InvalidInput, "case 517 is bad");
        }
        return {};
    });
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(r.error().message, "case 517 is bad");
}

TEST(WorkerPool, ExceptionBecomesInternalErrorAfterJoin) {
    const WorkerPool pool(4);
    std::atomic<int> done{0};
    const auto r = pool.run(1000, [&](std::size_t i, int) -> Result<void> {
        if (i == 300) {
            throw std::runtime_error("boom in worker");
        }
        done.fetch_add(1, std::memory_order_relaxed);
        return {};
    });
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::Internal);
    EXPECT_NE(r.error().message.find("boom in worker"), std::string::npos);
    EXPECT_LT(done.load(), 1000);
}

TEST(WorkerPool, CancelBeforeStartRunsNothing) {
    CancelToken cancel;
    cancel.requestCancel();
    std::atomic<int> calls{0};
    const WorkerPool pool(4);
    const auto r = pool.run(
        100,
        [&](std::size_t, int) -> Result<void> {
            calls.fetch_add(1);
            return {};
        },
        &cancel);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::Cancelled);
    EXPECT_EQ(calls.load(), 0);
}

TEST(WorkerPool, CancelDuringRunStopsRemainingItems) {
    CancelToken cancel;
    std::atomic<int> calls{0};
    const WorkerPool pool(2);
    const auto r = pool.run(
        100000,
        [&](std::size_t, int) -> Result<void> {
            if (calls.fetch_add(1) == 50) {
                cancel.requestCancel();
            }
            return {};
        },
        &cancel);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::Cancelled);
    EXPECT_LT(calls.load(), 100000);
}
