#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/common/Error.hpp"
#include "core/hw/Adapters.hpp"
#include "core/hw/Clock.hpp"

namespace uniforge {

class LogSink;

// Copy of one device's state for the UI, taken once per frame.
struct DeviceSnapshot {
    hw::DeviceInfo info;
    std::vector<hw::Capability> capabilities;
    hw::DeviceState state = hw::DeviceState::Disconnected;
    std::vector<hw::Telemetry> latest;          // last telemetry message
    std::vector<std::vector<float>> history;    // per channel of `latest`, oldest first
    std::optional<Error> lastError;             // set while Faulted
    std::string recoverySummary;                // "recovered after 1 retry", ...
    std::vector<hw::RetryAttempt> retryTrail;   // most recent recovery
};

struct HardwareSnapshot {
    bool discovered = false;
    std::vector<DeviceSnapshot> devices;
};

// Owns the simulated device adapters on its own thread (spec §6): a 10 Hz tick reads one
// telemetry message from every connected device into per-channel ring buffers. The UI posts
// commands into a mutex-guarded queue and reads a snapshot each frame; it never touches an
// adapter directly. A device that is timing out blocks the tick for up to the retry budget
// (≈ 1.7 s); the UI thread is unaffected.
class DeviceManager {
public:
    enum class Command { Discover, Connect, Disconnect, InjectTimeout, Retry, ResetFault };

    static constexpr std::size_t kHistoryCapacity = 120; // 12 s at 10 Hz

    explicit DeviceManager(LogSink* log, std::chrono::milliseconds tick =
                                             std::chrono::milliseconds(100));
    ~DeviceManager();
    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    // `device` is the index in the snapshot (ignored for Discover).
    void post(Command command, int device = -1);

    [[nodiscard]] HardwareSnapshot snapshot() const;
    // Incremented whenever the snapshot changes.
    [[nodiscard]] std::uint64_t version() const noexcept {
        return version_.load(std::memory_order_acquire);
    }
    // True while a command or tick is being processed (e.g. waiting out a simulated timeout).
    [[nodiscard]] bool busy() const noexcept { return busy_.load(std::memory_order_acquire); }

private:
    struct Pending {
        Command command;
        int device;
    };
    struct Channels {
        std::vector<std::deque<float>> values;
    };

    void threadMain();
    void execute(const Pending& p);
    void tick();
    void publish();
    void log(hw::EventLevel level, const std::string& text);

    LogSink* log_;
    std::chrono::milliseconds tickPeriod_;
    hw::SystemClock clock_;

    // Worker-thread state (no lock needed: only threadMain touches it).
    std::vector<std::unique_ptr<hw::MockAdapter>> devices_;
    std::vector<std::vector<hw::Telemetry>> latest_;
    std::vector<Channels> history_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Pending> queue_;
    bool stopping_ = false;
    HardwareSnapshot snapshot_;
    std::atomic<std::uint64_t> version_{0};
    std::atomic<bool> busy_{false};

    std::thread thread_; // last member: started once everything above is initialised
};

} // namespace uniforge
