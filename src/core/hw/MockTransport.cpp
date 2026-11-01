#include "core/hw/MockTransport.hpp"

#include <algorithm>
#include <string>

namespace uniforge::hw {

std::string_view toString(FailMode mode) noexcept {
    switch (mode) {
    case FailMode::None:
        return "None";
    case FailMode::TimeoutOnce:
        return "TimeoutOnce";
    case FailMode::TimeoutAlways:
        return "TimeoutAlways";
    case FailMode::Disconnect:
        return "Disconnect";
    }
    return "Unknown";
}

MockTransport::MockTransport(MockTransportConfig config, IClock& clock)
    : config_(config)
    , clock_(clock)
    , rng_(config.seed) {
    config_.latencyMs = std::max(0, config_.latencyMs);
    config_.jitterMs = std::max(0, config_.jitterMs);
    pendingTimeout_ = config_.failMode == FailMode::TimeoutOnce;
}

Result<void> MockTransport::open(std::chrono::milliseconds timeout) {
    if (open_) {
        return {};
    }
    Result<void> r = roundTrip(timeout, "open");
    if (r.ok()) {
        open_ = true;
    }
    return r;
}

void MockTransport::close() noexcept {
    open_ = false;
}

Result<void> MockTransport::exchange(std::chrono::milliseconds timeout) {
    if (!open_ && !unplugged_) {
        return makeError(ErrorCode::InvalidInput, "Simulated link is not open.",
                         "Connect the device first.");
    }
    if (config_.failMode == FailMode::Disconnect && !unplugged_ &&
        exchanges_ >= static_cast<std::uint64_t>(std::max(0, config_.disconnectAfter))) {
        unplugged_ = true;
        open_ = false;
    }
    Result<void> r = roundTrip(timeout, "exchange");
    if (r.ok()) {
        ++exchanges_;
    }
    return r;
}

void MockTransport::setFailMode(FailMode mode) noexcept {
    config_.failMode = mode;
    pendingTimeout_ = mode == FailMode::TimeoutOnce;
    exchanges_ = 0;
}

void MockTransport::clearFault() noexcept {
    config_.failMode = FailMode::None;
    pendingTimeout_ = false;
    unplugged_ = false;
    open_ = false;
    exchanges_ = 0;
}

Result<void> MockTransport::roundTrip(std::chrono::milliseconds timeout, std::string_view what) {
    if (unplugged_) {
        // A real host would see the USB device vanish; the request fails after the timeout.
        clock_.sleepFor(timeout);
        Error e = makeError(ErrorCode::DeviceNotFound,
                            "Simulated link lost during " + std::string(what) + ".",
                            "The simulated cable was unplugged. Press Reset fault to plug it "
                            "back in, then Connect.");
        e.retryable = true;
        return e;
    }
    const bool timeoutInjected = pendingTimeout_ || config_.failMode == FailMode::TimeoutAlways;
    pendingTimeout_ = false;
    const std::chrono::milliseconds latency = drawLatency();
    if (timeoutInjected || latency > timeout) {
        clock_.sleepFor(timeout);
        Error e = makeError(ErrorCode::DeviceTimeout,
                            "No reply to " + std::string(what) + " within " +
                                std::to_string(timeout.count()) + " ms.",
                            "Check the cable and the board's power, then press Retry.");
        e.retryable = true;
        return e;
    }
    clock_.sleepFor(latency);
    return {};
}

std::chrono::milliseconds MockTransport::drawLatency() {
    // Own mapping from raw 64-bit output (not std::uniform_int_distribution, whose output is
    // implementation-defined) keeps the sequence identical on MSVC, GCC and Clang.
    const std::uint64_t span = static_cast<std::uint64_t>(config_.jitterMs) + 1;
    const std::uint64_t jitter = rng_() % span;
    return std::chrono::milliseconds(config_.latencyMs + static_cast<int>(jitter));
}

} // namespace uniforge::hw
