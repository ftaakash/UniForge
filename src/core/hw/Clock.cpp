#include "core/hw/Clock.hpp"

namespace uniforge::hw {

SystemClock::SystemClock() : start_(std::chrono::steady_clock::now()) {}

std::chrono::milliseconds SystemClock::now() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_);
}

std::chrono::system_clock::time_point SystemClock::wallNow() const {
    return std::chrono::system_clock::now();
}

void SystemClock::sleepFor(std::chrono::milliseconds duration) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, duration, [this] { return interrupted_; });
}

void SystemClock::interrupt() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        interrupted_ = true;
    }
    cv_.notify_all();
}

std::chrono::system_clock::time_point ManualClock::wallNow() const {
    // Fixed epoch plus simulated time, so telemetry timestamps are reproducible too.
    return std::chrono::system_clock::time_point{} + now_;
}

} // namespace uniforge::hw
