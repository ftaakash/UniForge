#pragma once

#include <atomic>

namespace uniforge {

// Cooperative cancellation flag shared between the thread that requests a cancel (UI) and the
// thread doing the work. Long-running code polls cancelled() at natural checkpoints
// (per line batch, per case, per mesh, per repetition) and returns early when it is set.
class CancelToken {
public:
    void requestCancel() noexcept { cancelled_.store(true, std::memory_order_relaxed); }
    [[nodiscard]] bool cancelled() const noexcept {
        return cancelled_.load(std::memory_order_relaxed);
    }
    void reset() noexcept { cancelled_.store(false, std::memory_order_relaxed); }

private:
    std::atomic<bool> cancelled_{false};
};

} // namespace uniforge
