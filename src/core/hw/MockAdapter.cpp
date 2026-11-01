#include "core/hw/MockAdapter.hpp"

#include <utility>

namespace uniforge::hw {

namespace {

Error notAllowed(const DeviceInfo& info, const char* action, DeviceState state,
                 const char* hint) {
    return makeError(ErrorCode::InvalidInput,
                     std::string(action) + " is not possible while " + info.id + " is " +
                         std::string(toString(state)) + ".",
                     hint);
}

} // namespace

MockAdapter::MockAdapter(DeviceInfo info, MockDeviceConfig config, IClock& clock)
    : info_(std::move(info))
    , policy_(config.retry)
    , clock_(clock)
    , transport_(config.transport, clock)
    // Separate stream from the transport's so telemetry does not depend on latency draws.
    , rng_(config.transport.seed ^ 0x9E3779B97F4A7C15ull) {
    info_.simulated = true; // there is no other kind of adapter in UniForge
}

Result<void> MockAdapter::connect() {
    if (sm_.state() == DeviceState::Connected) {
        return {};
    }
    if (sm_.state() != DeviceState::Disconnected) {
        return notAllowed(info_, "Connect", sm_.state(),
                          "Press Retry, or Reset fault and connect again.");
    }
    sm_.transition(DeviceState::Connecting);
    emit(EventLevel::Info, "connecting over " + info_.transport);
    Result<void> r = transport_.open(policy_.timeout);
    if (!r.ok()) {
        return fail(r.error(), Op::Open);
    }
    sm_.transition(DeviceState::Connected);
    lastError_.reset();
    emit(EventLevel::Info, "connected");
    return {};
}

Result<void> MockAdapter::disconnect() {
    const DeviceState s = sm_.state();
    if (s == DeviceState::Disconnected) {
        return {};
    }
    if (s != DeviceState::Connected && s != DeviceState::Faulted) {
        return notAllowed(info_, "Disconnect", s, "Wait for the current request to finish.");
    }
    transport_.close();
    sm_.transition(DeviceState::Disconnected);
    emit(EventLevel::Info, "disconnected");
    return {};
}

Result<std::vector<Telemetry>> MockAdapter::readTelemetry() {
    if (sm_.state() != DeviceState::Connected) {
        return notAllowed(info_, "Reading telemetry", sm_.state(), "Connect the device first.");
    }
    Result<void> r = transport_.exchange(policy_.timeout);
    if (!r.ok()) {
        Result<void> recovered = fail(r.error(), Op::Exchange);
        if (!recovered.ok()) {
            return std::move(recovered).error();
        }
    }
    ++samples_;
    return sample();
}

Result<void> MockAdapter::retry() {
    if (sm_.state() != DeviceState::Faulted) {
        return notAllowed(info_, "Retry", sm_.state(), "Retry is only needed after a fault.");
    }
    if (!lastError_) {
        lastError_ = makeError(ErrorCode::DeviceFault, "Unknown fault.");
    }
    lastError_->retryable = true; // the user explicitly asked to try again
    return fail(*lastError_, lastOp_);
}

Result<void> MockAdapter::resetFault() {
    if (sm_.state() != DeviceState::Faulted) {
        return notAllowed(info_, "Reset fault", sm_.state(), "There is no fault to reset.");
    }
    transport_.clearFault();
    sm_.transition(DeviceState::Disconnected);
    lastError_.reset();
    trail_.clear();
    summary_.clear();
    emit(EventLevel::Info, "fault reset; link cleared and disconnected");
    return {};
}

Result<void> MockAdapter::fail(const Error& error, Op op) {
    lastOp_ = op;
    if (sm_.state() != DeviceState::Faulted) {
        sm_.transition(DeviceState::Faulted);
    }
    lastError_ = error;
    emit(EventLevel::Warn, error.message + " Retrying.");

    sm_.transition(DeviceState::Recovering);
    RetryOutcome outcome = retryAfterFailure(
        error, policy_, clock_, [this](std::chrono::milliseconds timeout) {
            return lastOp_ == Op::Open ? transport_.open(timeout) : transport_.exchange(timeout);
        });
    trail_ = std::move(outcome.trail);
    summary_ = outcome.summary;
    if (outcome.result.ok()) {
        sm_.transition(DeviceState::Connected);
        lastError_.reset();
        emit(EventLevel::Info, summary_);
        return {};
    }
    sm_.transition(DeviceState::Faulted);
    lastError_ = outcome.result.error();
    emit(EventLevel::Error, lastError_->message + " " + lastError_->hint);
    return outcome.result;
}

void MockAdapter::emit(EventLevel level, const std::string& text) const {
    if (events_) {
        events_(level, info_.id + ": " + text);
    }
}

double MockAdapter::uniform() {
    return static_cast<double>(rng_() >> 11) * 0x1.0p-53;
}

Telemetry MockAdapter::make(std::string channel, double value, std::string unit) const {
    return Telemetry{clock_.wallNow(), std::move(channel), value, std::move(unit)};
}

} // namespace uniforge::hw
