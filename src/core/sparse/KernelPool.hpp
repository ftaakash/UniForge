#pragma once

#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "core/sparse/CsrMatrix.hpp"

namespace uniforge::sparse {

// Persistent thread pool for the sparse kernels (spec §5.5, SysDesign S2).
//
// v1's perf::WorkerPool starts and joins new threads on every run() call. That suits a batch of
// independent rod problems, but a solver calls its kernels thousands of times, so here the
// helper threads start once and wait between calls.
//
// Determinism (spec N2): work is split into fixed chunks of kChunkRows rows, independent of the
// number of threads. Chunk c is processed by thread c mod p, and a reduction stores one partial
// result per chunk that the calling thread combines in chunk order. The floating-point result
// is therefore bit-identical for every thread count, including p = 1.
//
// The calling thread is worker 0 and takes part in every call, so KernelPool(1) starts no
// helper threads. Calls are not reentrant: a body must not call back into the same pool, and
// only one thread may call the pool at a time.
class KernelPool {
public:
    static constexpr Index kChunkRows = 4096;

    // Called once on each helper thread (index 1 … p−1) before it takes work, e.g. to pin it to
    // a core (the OS-specific code lives in cli/platform, spec §4.2).
    using ThreadStartFn = std::function<void(int threadIndex)>;

    // `threads` < 1 is treated as 1.
    explicit KernelPool(int threads, ThreadStartFn onThreadStart = {});
    ~KernelPool();
    KernelPool(const KernelPool&) = delete;
    KernelPool& operator=(const KernelPool&) = delete;

    [[nodiscard]] int threads() const noexcept { return threads_; }

    // Number of chunks for n rows: ceil(n / kChunkRows).
    [[nodiscard]] static Index chunkCount(Index n) noexcept;

    // Runs body(begin, end) on every chunk [c·kChunkRows, min(n, (c+1)·kChunkRows)). Each row is
    // visited exactly once. If a body throws, the first exception is rethrown here after all
    // threads have finished the call.
    void parallelFor(Index n, const std::function<void(Index begin, Index end)>& body);

    // Σ over chunks of partial(begin, end), summed in chunk order: bit-identical for any p.
    [[nodiscard]] double reduceSum(Index n,
                                   const std::function<double(Index begin, Index end)>& partial);

    // max over chunks of partial(begin, end); −infinity for n = 0; NaN if any partial is NaN.
    [[nodiscard]] double reduceMax(Index n,
                                   const std::function<double(Index begin, Index end)>& partial);

private:
    void runChunks(Index n, const std::function<void(Index chunk, Index begin, Index end)>& body);
    void workerLoop(int index);
    void processChunks(int index);

    int threads_;
    std::vector<std::thread> helpers_;
    std::vector<double> partial_; // one slot per chunk, reused between calls

    std::mutex mutex_;
    std::condition_variable start_;
    std::condition_variable done_;
    std::uint64_t generation_ = 0; // guarded by mutex_
    int pending_ = 0;              // helpers still working on the current generation
    bool quit_ = false;

    // The current call, published to helpers under mutex_ when generation_ changes.
    const std::function<void(Index, Index, Index)>* body_ = nullptr;
    Index n_ = 0;
    std::exception_ptr firstException_; // guarded by mutex_
};

} // namespace uniforge::sparse
