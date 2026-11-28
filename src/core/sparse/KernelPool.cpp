#include "core/sparse/KernelPool.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <system_error>
#include <utility>

namespace uniforge::sparse {

KernelPool::KernelPool(int threads, ThreadStartFn onThreadStart)
    : threads_(std::max(1, threads)) {
    helpers_.reserve(toSize(static_cast<Index>(threads_ - 1)));
    for (int i = 1; i < threads_; ++i) {
        try {
            helpers_.emplace_back([this, i, onThreadStart] {
                if (onThreadStart) {
                    onThreadStart(i);
                }
                workerLoop(i);
            });
        } catch (const std::system_error&) {
            // The OS refused another thread. Carry on with the threads that did start: the
            // chunking does not depend on the thread count, so results stay identical.
            threads_ = i;
            break;
        }
    }
}

KernelPool::~KernelPool() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
    }
    start_.notify_all();
    for (std::thread& t : helpers_) {
        t.join();
    }
}

Index KernelPool::chunkCount(Index n) noexcept {
    return n <= 0 ? 0 : (n - 1) / kChunkRows + 1;
}

void KernelPool::workerLoop(int index) {
    std::uint64_t seen = 0;
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        start_.wait(lock, [&] { return quit_ || generation_ != seen; });
        if (quit_) {
            return;
        }
        seen = generation_;
        lock.unlock();
        processChunks(index);
        lock.lock();
        if (--pending_ == 0) {
            done_.notify_one();
        }
    }
}

void KernelPool::processChunks(int index) {
    const Index chunks = chunkCount(n_);
    for (Index c = index; c < chunks; c += threads_) {
        const Index begin = c * kChunkRows;
        const Index end = std::min(n_, begin + kChunkRows);
        try {
            (*body_)(c, begin, end);
        } catch (...) {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (!firstException_) {
                firstException_ = std::current_exception();
            }
        }
    }
}

void KernelPool::runChunks(Index n,
                           const std::function<void(Index chunk, Index begin, Index end)>& body) {
    const Index chunks = chunkCount(n);
    if (chunks == 0) {
        return;
    }
    if (threads_ == 1 || chunks == 1) {
        // Same chunks, same order of partial results, no thread hand-off.
        for (Index c = 0; c < chunks; ++c) {
            const Index begin = c * kChunkRows;
            body(c, begin, std::min(n, begin + kChunkRows));
        }
        return;
    }
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        body_ = &body;
        n_ = n;
        firstException_ = nullptr;
        pending_ = threads_ - 1;
        ++generation_;
    }
    start_.notify_all();
    processChunks(0);
    std::exception_ptr failure;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [&] { return pending_ == 0; });
        body_ = nullptr;
        failure = std::exchange(firstException_, nullptr);
    }
    if (failure) {
        std::rethrow_exception(failure);
    }
}

void KernelPool::parallelFor(Index n, const std::function<void(Index begin, Index end)>& body) {
    runChunks(n, [&body](Index, Index begin, Index end) { body(begin, end); });
}

double KernelPool::reduceSum(Index n,
                             const std::function<double(Index begin, Index end)>& partial) {
    const Index chunks = chunkCount(n);
    if (partial_.size() < toSize(chunks)) {
        partial_.resize(toSize(chunks));
    }
    runChunks(n, [this, &partial](Index c, Index begin, Index end) {
        partial_[toSize(c)] = partial(begin, end);
    });
    double sum = 0.0;
    for (Index c = 0; c < chunks; ++c) {
        sum += partial_[toSize(c)];
    }
    return sum;
}

double KernelPool::reduceMax(Index n,
                             const std::function<double(Index begin, Index end)>& partial) {
    const Index chunks = chunkCount(n);
    if (partial_.size() < toSize(chunks)) {
        partial_.resize(toSize(chunks));
    }
    runChunks(n, [this, &partial](Index c, Index begin, Index end) {
        partial_[toSize(c)] = partial(begin, end);
    });
    double best = -std::numeric_limits<double>::infinity();
    for (Index c = 0; c < chunks; ++c) {
        const double v = partial_[toSize(c)];
        if (std::isnan(v)) {
            return v; // propagate NaN instead of letting max() skip it
        }
        best = std::max(best, v);
    }
    return best;
}

} // namespace uniforge::sparse
