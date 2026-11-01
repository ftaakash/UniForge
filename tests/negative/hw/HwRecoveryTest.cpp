// Fault injection and recovery (spec §7.5): TimeoutOnce recovers after 1 retry, TimeoutAlways
// faults after 3 retries with a hint, Disconnect mid-session, and the user's Retry / Reset.

#include "core/hw/Adapters.hpp"

#include <chrono>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/hw/RetryPolicy.hpp"

using namespace std::chrono_literals;
using namespace uniforge::hw;
using uniforge::ErrorCode;

namespace {

struct EventLog {
    std::vector<std::string> lines;
    EventFn sink() {
        return [this](EventLevel, const std::string& text) { lines.push_back(text); };
    }
    [[nodiscard]] bool contains(const std::string& needle) const {
        for (const std::string& l : lines) {
            if (l.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};

std::unique_ptr<MockAdapter> mcuWith(FailMode mode, ManualClock& clock) {
    DiscoveryConfig cfg;
    cfg.mcuFail = mode;
    return std::move(discoverDevices(cfg, clock)[0]);
}

} // namespace

TEST(HwRecovery, TimeoutOnceRecoversAfterOneRetry) {
    ManualClock clock;
    EventLog log;
    auto mcu = mcuWith(FailMode::TimeoutOnce, clock);
    mcu->setEventSink(log.sink());
    ASSERT_TRUE(mcu->connect().ok());
    EXPECT_EQ(mcu->state(), DeviceState::Connected);
    EXPECT_EQ(mcu->lastRecoverySummary(), "recovered after 1 retry");
    ASSERT_EQ(mcu->retryTrail().size(), 1u);
    EXPECT_EQ(mcu->retryTrail()[0].backoff, 100ms);
    EXPECT_TRUE(mcu->retryTrail()[0].ok);
    EXPECT_TRUE(log.contains("recovered after 1 retry"));
    EXPECT_EQ(mcu->stateHistory(),
              (std::vector<DeviceState>{DeviceState::Disconnected, DeviceState::Connecting,
                                        DeviceState::Faulted, DeviceState::Recovering,
                                        DeviceState::Connected}));
    // 250 ms timeout + 100 ms back-off + one successful round trip (20..30 ms).
    EXPECT_GE(clock.now(), 370ms);
    EXPECT_LE(clock.now(), 380ms);
}

TEST(HwRecovery, TimeoutAlwaysFaultsAfterThreeRetriesWithAHint) {
    ManualClock clock;
    auto mcu = mcuWith(FailMode::TimeoutAlways, clock);
    auto r = mcu->connect();
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::DeviceTimeout);
    EXPECT_NE(r.error().hint.find("Gave up after 3 retries"), std::string::npos)
        << r.error().hint;
    EXPECT_NE(r.error().hint.find("press Retry"), std::string::npos) << r.error().hint;
    EXPECT_EQ(mcu->state(), DeviceState::Faulted);
    ASSERT_TRUE(mcu->lastError().has_value());
    EXPECT_EQ(mcu->lastRecoverySummary(), "failed after 3 retries");
    ASSERT_EQ(mcu->retryTrail().size(), 3u);
    EXPECT_EQ(mcu->retryTrail()[0].backoff, 100ms);
    EXPECT_EQ(mcu->retryTrail()[1].backoff, 200ms);
    EXPECT_EQ(mcu->retryTrail()[2].backoff, 400ms);
    // 4 timeouts of 250 ms + 700 ms of back-off.
    EXPECT_EQ(clock.now(), 1700ms);
}

TEST(HwRecovery, FaultedDeviceRefusesConnectUntilReset) {
    ManualClock clock;
    auto mcu = mcuWith(FailMode::TimeoutAlways, clock);
    ASSERT_FALSE(mcu->connect().ok());
    auto again = mcu->connect();
    ASSERT_FALSE(again.ok());
    EXPECT_EQ(again.error().code, ErrorCode::InvalidInput);
    ASSERT_TRUE(mcu->resetFault().ok());
    EXPECT_EQ(mcu->state(), DeviceState::Disconnected);
    EXPECT_FALSE(mcu->lastError().has_value());
    ASSERT_TRUE(mcu->connect().ok()); // reset cleared the injected fault
}

TEST(HwRecovery, UserRetryRecoversOnceTheFaultIsGone) {
    ManualClock clock;
    auto mcu = mcuWith(FailMode::TimeoutAlways, clock);
    ASSERT_FALSE(mcu->connect().ok());
    EXPECT_FALSE(mcu->retry().ok()); // still failing
    mcu->setFailMode(FailMode::None);
    ASSERT_TRUE(mcu->retry().ok());
    EXPECT_EQ(mcu->state(), DeviceState::Connected);
    EXPECT_EQ(mcu->lastRecoverySummary(), "recovered after 1 retry");
}

TEST(HwRecovery, InjectedTimeoutDuringTelemetryRecovers) {
    ManualClock clock;
    EventLog log;
    auto fpga = std::move(discoverDevices({}, clock)[1]);
    fpga->setEventSink(log.sink());
    ASSERT_TRUE(fpga->connect().ok());
    fpga->injectTimeoutOnce();
    auto t = fpga->readTelemetry();
    ASSERT_TRUE(t.ok());
    EXPECT_EQ(t.value().size(), 2u);
    EXPECT_EQ(fpga->state(), DeviceState::Connected);
    EXPECT_TRUE(log.contains("SIM-FPGA-0001: recovered after 1 retry"));
}

TEST(HwRecovery, DisconnectMidSessionFaultsAndResetRestoresTheLink) {
    ManualClock clock;
    DiscoveryConfig cfg;
    cfg.jtagFail = FailMode::Disconnect;
    auto jtag = std::move(discoverDevices(cfg, clock)[2]);
    ASSERT_TRUE(jtag->connect().ok());
    for (int i = 0; i < 3; ++i) { // MockTransportConfig::disconnectAfter = 3
        ASSERT_TRUE(jtag->readTelemetry().ok()) << i;
    }
    auto lost = jtag->readTelemetry();
    ASSERT_FALSE(lost.ok());
    EXPECT_EQ(lost.error().code, ErrorCode::DeviceFault);
    EXPECT_NE(lost.error().message.find("link lost"), std::string::npos);
    EXPECT_NE(lost.error().hint.find("Reset fault"), std::string::npos);
    EXPECT_EQ(jtag->state(), DeviceState::Faulted);
    EXPECT_EQ(jtag->retryTrail().size(), 3u);

    ASSERT_TRUE(jtag->resetFault().ok());
    ASSERT_TRUE(jtag->connect().ok());
    EXPECT_TRUE(jtag->readTelemetry().ok());
}

TEST(HwRecovery, DisconnectFromFaultedIsAllowed) {
    ManualClock clock;
    auto mcu = mcuWith(FailMode::TimeoutAlways, clock);
    ASSERT_FALSE(mcu->connect().ok());
    ASSERT_TRUE(mcu->disconnect().ok());
    EXPECT_EQ(mcu->state(), DeviceState::Disconnected);
}

TEST(HwRecovery, ActionsInTheWrongStateAreRejected) {
    ManualClock clock;
    auto mcu = mcuWith(FailMode::None, clock);
    EXPECT_EQ(mcu->retry().error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(mcu->resetFault().error().code, ErrorCode::InvalidInput);
}

TEST(HwRetryPolicy, NonRetryableErrorsAreNotRetried) {
    ManualClock clock;
    int calls = 0;
    uniforge::Error e = uniforge::makeError(ErrorCode::DeviceFault, "broken");
    RetryOutcome out = retryAfterFailure(e, RetryPolicy{}, clock, [&](std::chrono::milliseconds) {
        ++calls;
        return uniforge::Result<void>();
    });
    EXPECT_FALSE(out.result.ok());
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(out.retries, 0);
    EXPECT_EQ(clock.now(), 0ms);
}

TEST(HwRetryPolicy, UsesTheSpecifiedTimeoutAndBackoff) {
    RetryPolicy p;
    EXPECT_EQ(p.timeout, 250ms);
    EXPECT_EQ(p.maxRetries(), 3);
    EXPECT_EQ(p.backoff[0], 100ms);
    EXPECT_EQ(p.backoff[1], 200ms);
    EXPECT_EQ(p.backoff[2], 400ms);
}
