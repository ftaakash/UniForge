#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

#include "app/services/DemoJob.hpp"

namespace uniforge {

// Holds the latest successful result of one domain as an immutable shared_ptr.
// Readers (UI thread) get a snapshot they can keep using while a new job runs; a writer
// replaces the whole pointer, so a reader never sees a half-written result.
template <class T> class ResultSlot {
public:
    [[nodiscard]] std::shared_ptr<const T> get() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return value_;
    }

    void publish(std::shared_ptr<const T> value) {
        std::lock_guard<std::mutex> lock(mutex_);
        value_ = std::move(value);
        ++generation_;
    }

    // Number of successful publishes so far.
    [[nodiscard]] std::uint64_t generation() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return generation_;
    }

private:
    mutable std::mutex mutex_;
    std::shared_ptr<const T> value_;
    std::uint64_t generation_ = 0;
};

// One slot per result domain. Later phases add thermal, geometry, benchmark and backprop slots.
struct ResultStore {
    ResultSlot<DemoJobResult> demo;
};

} // namespace uniforge
