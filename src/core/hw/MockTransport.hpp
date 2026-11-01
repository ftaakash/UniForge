#pragma once

#include <chrono>
#include <cstdint>
#include <random>
#include <string_view>

#include "core/common/Result.hpp"
#include "core/hw/Clock.hpp"

namespace uniforge::hw {

// Fault the simulated link injects (spec §7.5).
//  None          every request succeeds after latency + jitter.
//  TimeoutOnce   the next request times out, later ones succeed.
//  TimeoutAlways every request times out.
//  Disconnect    the link drops after `disconnectAfter` successful exchanges and stays down
//                until clearFault() (a simulated unplug).
enum class FailMode { None, TimeoutOnce, TimeoutAlways, Disconnect };

[[nodiscard]] std::string_view toString(FailMode mode) noexcept;

struct MockTransportConfig {
    std::uint64_t seed = 1;
    int latencyMs = 20;
    int jitterMs = 10;
    FailMode failMode = FailMode::None;
    int disconnectAfter = 3; // successful exchanges before a Disconnect fault drops the link
};

// Simulated host link standing in for a USB VCP/JTAG/SWD connection. No I/O is performed: each
// request just spends simulated latency on the injected clock and either succeeds or fails
// according to the fail mode. Deterministic for a given seed and clock.
class MockTransport {
public:
    MockTransport(MockTransportConfig config, IClock& clock);

    // Opens the link. Fails with DeviceTimeout (retryable) when a timeout is injected or the
    // latency exceeds `timeout`, or with DeviceNotFound when the link is unplugged.
    Result<void> open(std::chrono::milliseconds timeout);
    void close() noexcept;
    [[nodiscard]] bool isOpen() const noexcept { return open_; }

    // One request/response round trip on an open link.
    Result<void> exchange(std::chrono::milliseconds timeout);

    // Arms a one-shot timeout for the next request (the UI's "Inject timeout").
    void injectTimeoutOnce() noexcept { pendingTimeout_ = true; }
    void setFailMode(FailMode mode) noexcept;
    [[nodiscard]] FailMode failMode() const noexcept { return config_.failMode; }
    // Clears every injected fault and "plugs the cable back in". The link stays closed.
    void clearFault() noexcept;

    [[nodiscard]] std::uint64_t exchanges() const noexcept { return exchanges_; }

private:
    Result<void> roundTrip(std::chrono::milliseconds timeout, std::string_view what);
    [[nodiscard]] std::chrono::milliseconds drawLatency();

    MockTransportConfig config_;
    IClock& clock_;
    std::mt19937_64 rng_;
    bool open_ = false;
    bool unplugged_ = false;
    bool pendingTimeout_ = false;
    std::uint64_t exchanges_ = 0;
};

} // namespace uniforge::hw
