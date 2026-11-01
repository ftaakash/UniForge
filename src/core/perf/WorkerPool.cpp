#include "core/perf/WorkerPool.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace uniforge::perf {

namespace {

Error cancelledError() {
    return makeError(ErrorCode::Cancelled, "Batch run cancelled.",
                     "No results were published; run it again when you are ready.");
}

std::string describe(const std::exception_ptr& e) {
    try {
        std::rethrow_exception(e);
    } catch (const std::exception& ex) {
        return ex.what();
    } catch (...) {
        return "unknown exception";
    }
}

} // namespace

WorkerPool::WorkerPool(int workers)
    : workers_(std::max(1, workers)) {}

int WorkerPool::threadsFor(std::size_t count) const noexcept {
    if (count == 0) {
        return 1;
    }
    return static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(workers_), count));
}

std::size_t WorkerPool::chunkSize(std::size_t count, int workers) noexcept {
    const std::size_t p = static_cast<std::size_t>(std::max(1, workers));
    return std::max<std::size_t>(1, count / (8 * p));
}

Result<void> WorkerPool::run(std::size_t count, const Body& body,
                             const CancelToken* cancel) const {
    if (cancel != nullptr && cancel->cancelled()) {
        return cancelledError();
    }
    if (count == 0) {
        return {};
    }
    const int threads = threadsFor(count);
    const std::size_t chunk = chunkSize(count, workers_);

    std::atomic<std::size_t> next{0};
    std::atomic<bool> stop{false};
    std::mutex failMutex; // only taken on the failure path
    std::optional<Error> firstError;
    std::exception_ptr firstException;

    const auto work = [&](int worker) {
        try {
            while (!stop.load(std::memory_order_relaxed)) {
                const std::size_t begin = next.fetch_add(chunk, std::memory_order_relaxed);
                if (begin >= count) {
                    return;
                }
                const std::size_t end = std::min(begin + chunk, count);
                for (std::size_t i = begin; i < end; ++i) {
                    if (cancel != nullptr && cancel->cancelled()) {
                        stop.store(true, std::memory_order_relaxed);
                        return;
                    }
                    if (auto r = body(i, worker); !r) {
                        const std::lock_guard<std::mutex> lock(failMutex);
                        if (!firstError) {
                            firstError = std::move(r).error();
                        }
                        stop.store(true, std::memory_order_relaxed);
                        return;
                    }
                }
            }
        } catch (...) {
            const std::lock_guard<std::mutex> lock(failMutex);
            if (!firstException) {
                firstException = std::current_exception();
            }
            stop.store(true, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(threads));
    std::string startFailure;
    try {
        for (int w = 0; w < threads; ++w) {
            pool.emplace_back(work, w);
        }
    } catch (const std::system_error& e) {
        stop.store(true, std::memory_order_relaxed);
        startFailure = e.what();
    }
    for (std::thread& t : pool) {
        t.join();
    }

    if (!startFailure.empty()) {
        return makeError(ErrorCode::Internal,
                         "Could not start " + std::to_string(threads) +
                             " worker threads: " + startFailure + ".",
                         "Try fewer workers or close other programs.");
    }
    if (firstException) {
        return makeError(ErrorCode::Internal,
                         "A worker thread failed: " + describe(firstException) + ".",
                         "This is a bug or the machine ran out of memory; see the log.");
    }
    if (firstError) {
        return std::move(*firstError);
    }
    if (cancel != nullptr && cancel->cancelled()) {
        return cancelledError();
    }
    return {};
}

} // namespace uniforge::perf
