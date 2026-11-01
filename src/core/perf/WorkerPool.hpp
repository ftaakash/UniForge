#pragma once

#include <cstddef>
#include <functional>

#include "core/common/CancelToken.hpp"
#include "core/common/Result.hpp"

namespace uniforge::perf {

// Bounded fork-join pool for the Task D batch (spec §6).
//
// run() starts min(p, count) std::threads, hands out indices in chunks from one
// std::atomic<size_t> (chunk = max(1, count / (8p)), so a slow core picks up fewer chunks
// instead of holding up the end), and joins every thread before it returns. No locks in the
// hot path: the body writes only its own out[i], and join() makes those writes visible to the
// caller. The first error returned by the body, an exception thrown by it (turned into
// Error{Internal}) or a cancel request stops the remaining chunks.
class WorkerPool {
public:
    // body(index, worker): processes item `index` on worker `worker` (0 ≤ worker < p), so
    // per-worker scratch buffers can be indexed by `worker`.
    using Body = std::function<Result<void>(std::size_t index, int worker)>;

    // `workers` < 1 is treated as 1.
    explicit WorkerPool(int workers);

    [[nodiscard]] int workers() const noexcept { return workers_; }

    // Threads run() actually starts for `count` items: min(p, count), at least 1.
    [[nodiscard]] int threadsFor(std::size_t count) const noexcept;

    // max(1, count / (8p))
    [[nodiscard]] static std::size_t chunkSize(std::size_t count, int workers) noexcept;

    // Errors: the body's first error; Internal for an exception escaping the body or a thread
    // that cannot be started; Cancelled when `cancel` was set (checked before every item).
    Result<void> run(std::size_t count, const Body& body,
                     const CancelToken* cancel = nullptr) const;

private:
    int workers_;
};

} // namespace uniforge::perf
