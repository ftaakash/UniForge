#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace uniforge::hw {

// Time source for the mocks. Latency, timeouts and retry back-off all go through it, so tests
// use ManualClock and finish instantly and deterministically, while the app uses SystemClock.
class IClock {
public:
    virtual ~IClock() = default;
    // Monotonic time since the clock was created.
    [[nodiscard]] virtual std::chrono::milliseconds now() const = 0;
    // Wall-clock time for telemetry timestamps.
    [[nodiscard]] virtual std::chrono::system_clock::time_point wallNow() const = 0;
    virtual void sleepFor(std::chrono::milliseconds duration) = 0;
};

// Real time. sleepFor() can be cut short by interrupt() so that an owner thread blocked in a
// retry back-off can shut down promptly.
class SystemClock final : public IClock {
public:
    SystemClock();
    [[nodiscard]] std::chrono::milliseconds now() const override;
    [[nodiscard]] std::chrono::system_clock::time_point wallNow() const override;
    void sleepFor(std::chrono::milliseconds duration) override;
    void interrupt();

private:
    std::chrono::steady_clock::time_point start_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool interrupted_ = false;
};

// Simulated time: sleepFor() advances the clock without waiting. Not thread-safe; for tests and
// single-threaded use.
class ManualClock final : public IClock {
public:
    [[nodiscard]] std::chrono::milliseconds now() const override { return now_; }
    [[nodiscard]] std::chrono::system_clock::time_point wallNow() const override;
    void sleepFor(std::chrono::milliseconds duration) override { now_ += duration; }
    void advance(std::chrono::milliseconds duration) { now_ += duration; }

private:
    std::chrono::milliseconds now_{0};
};

} // namespace uniforge::hw
