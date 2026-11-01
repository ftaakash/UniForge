#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "core/hw/Clock.hpp"
#include "core/hw/DeviceStateMachine.hpp"
#include "core/hw/IDeviceAdapter.hpp"
#include "core/hw/MockTransport.hpp"
#include "core/hw/RetryPolicy.hpp"

namespace uniforge::hw {

enum class EventLevel { Info, Warn, Error };
// Receives human-readable events ("connected", "recovered after 1 retry", ...).
using EventFn = std::function<void(EventLevel, const std::string&)>;

struct MockDeviceConfig {
    MockTransportConfig transport;
    RetryPolicy retry;
};

// Shared behaviour of the three simulated devices: the state machine, the simulated link and
// the retry policy. Subclasses only describe themselves and synthesise their telemetry.
//
// A failed request moves the device to Faulted and immediately runs recovery (Recovering, up to
// 3 retries with back-off). Success returns to Connected and logs "recovered after n retries";
// exhaustion stays in Faulted with lastError() holding the message and the hint.
class MockAdapter : public IDeviceAdapter {
public:
    MockAdapter(DeviceInfo info, MockDeviceConfig config, IClock& clock);

    [[nodiscard]] const DeviceInfo& info() const override { return info_; }
    [[nodiscard]] DeviceState state() const override { return sm_.state(); }
    [[nodiscard]] bool isSimulated() const override { return info_.simulated; }

    Result<void> connect() override;
    Result<void> disconnect() override;
    Result<std::vector<Telemetry>> readTelemetry() override;

    // Fault controls for the Hardware view.
    void injectTimeoutOnce() { transport_.injectTimeoutOnce(); }
    void setFailMode(FailMode mode) { transport_.setFailMode(mode); }
    // From Faulted: runs the recovery loop again (user "Retry").
    Result<void> retry();
    // From Faulted: clears injected faults, closes the link and returns to Disconnected.
    Result<void> resetFault();

    void setEventSink(EventFn fn) { events_ = std::move(fn); }
    [[nodiscard]] const std::optional<Error>& lastError() const noexcept { return lastError_; }
    // Trail of the most recent recovery (cleared when a new one starts).
    [[nodiscard]] const std::vector<RetryAttempt>& retryTrail() const noexcept { return trail_; }
    [[nodiscard]] const std::string& lastRecoverySummary() const noexcept { return summary_; }
    [[nodiscard]] const std::vector<DeviceState>& stateHistory() const noexcept {
        return sm_.history();
    }

protected:
    // One telemetry message: one sample per channel, values derived from `uniform` draws.
    virtual std::vector<Telemetry> sample() = 0;

    // Uniform double in [0, 1) from the device's own seeded generator (portable mapping).
    [[nodiscard]] double uniform();
    [[nodiscard]] Telemetry make(std::string channel, double value, std::string unit) const;
    [[nodiscard]] std::uint64_t samplesTaken() const noexcept { return samples_; }

private:
    enum class Op { Open, Exchange };
    Result<void> fail(const Error& error, Op op);
    void emit(EventLevel level, const std::string& text) const;

    DeviceInfo info_;
    RetryPolicy policy_;
    IClock& clock_;
    MockTransport transport_;
    DeviceStateMachine sm_;
    std::mt19937_64 rng_;
    EventFn events_;
    std::optional<Error> lastError_;
    std::vector<RetryAttempt> trail_;
    std::string summary_;
    Op lastOp_ = Op::Open;
    std::uint64_t samples_ = 0;
};

} // namespace uniforge::hw
