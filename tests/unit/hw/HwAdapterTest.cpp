// Simulated device adapters (spec §7.5, E3): discovery, capabilities, connect/disconnect,
// telemetry, determinism and the state machine. All on a ManualClock, so no wall time passes.

#include "core/hw/Adapters.hpp"

#include <algorithm>
#include <chrono>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/hw/DeviceStateMachine.hpp"
#include "core/hw/MockTransport.hpp"

using namespace std::chrono_literals;
using namespace uniforge::hw;

namespace {

std::vector<std::string> capabilityNames(const IDeviceAdapter& a) {
    std::vector<std::string> names;
    for (const Capability& c : a.capabilities()) {
        names.push_back(c.name);
    }
    return names;
}

std::vector<std::string> channels(const std::vector<Telemetry>& t) {
    std::vector<std::string> names;
    for (const Telemetry& s : t) {
        names.push_back(s.channel);
    }
    return names;
}

} // namespace

TEST(HwDiscovery, ReturnsMcuFpgaAndJtagWithUniqueSimulatedIds) {
    ManualClock clock;
    auto devices = discoverDevices({}, clock);
    ASSERT_EQ(devices.size(), 3u);
    EXPECT_EQ(devices[0]->info().kind, DeviceKind::Microcontroller);
    EXPECT_EQ(devices[1]->info().kind, DeviceKind::Fpga);
    EXPECT_EQ(devices[2]->info().kind, DeviceKind::JtagProbe);
    std::set<std::string> ids;
    for (const auto& d : devices) {
        ids.insert(d->info().id);
        EXPECT_EQ(d->info().id.rfind("SIM-", 0), 0u) << d->info().id;
        EXPECT_NE(d->info().transport.find("simulated"), std::string::npos);
        EXPECT_EQ(d->state(), DeviceState::Disconnected);
    }
    EXPECT_EQ(ids.size(), 3u);
}

TEST(HwDiscovery, EveryAdapterIsSimulated) {
    ManualClock clock;
    for (const auto& d : discoverDevices({}, clock)) {
        EXPECT_TRUE(d->isSimulated()) << d->info().id;
        EXPECT_TRUE(d->info().simulated) << d->info().id;
    }
}

TEST(HwCapabilities, MatchTheDeviceKind) {
    ManualClock clock;
    auto devices = discoverDevices({}, clock);
    EXPECT_EQ(capabilityNames(*devices[0]),
              (std::vector<std::string>{"UART telemetry", "GPIO"}));
    EXPECT_EQ(capabilityNames(*devices[1]),
              (std::vector<std::string>{"XADC temperature", "DONE status"}));
    EXPECT_EQ(capabilityNames(*devices[2]),
              (std::vector<std::string>{"IDCODE scan", "VTref sense"}));
}

TEST(HwConnect, ConnectAndDisconnectAreIdempotent) {
    ManualClock clock;
    for (const auto& d : discoverDevices({}, clock)) {
        EXPECT_TRUE(d->disconnect().ok()); // already disconnected: no-op
        ASSERT_TRUE(d->connect().ok());
        EXPECT_EQ(d->state(), DeviceState::Connected);
        EXPECT_TRUE(d->connect().ok()); // already connected: no-op
        EXPECT_EQ(d->state(), DeviceState::Connected);
        EXPECT_TRUE(d->disconnect().ok());
        EXPECT_EQ(d->state(), DeviceState::Disconnected);
        EXPECT_TRUE(d->disconnect().ok());
        EXPECT_EQ(d->state(), DeviceState::Disconnected);
    }
}

TEST(HwConnect, ConnectSpendsSimulatedLatencyOnly) {
    ManualClock clock;
    DiscoveryConfig cfg;
    cfg.latencyMs = 20;
    cfg.jitterMs = 10;
    auto devices = discoverDevices(cfg, clock);
    ASSERT_TRUE(devices[0]->connect().ok());
    EXPECT_GE(clock.now(), 20ms);
    EXPECT_LE(clock.now(), 30ms);
}

TEST(HwTelemetry, OneMessagePerReadWithTheSpecifiedChannels) {
    ManualClock clock;
    auto devices = discoverDevices({}, clock);
    const std::vector<std::vector<std::string>> expected{
        {"vdd_V", "die_temp_C"}, {"xadc_temp_C", "done"}, {"vtref_V", "idcode"}};
    for (std::size_t i = 0; i < devices.size(); ++i) {
        ASSERT_TRUE(devices[i]->connect().ok());
        auto t = devices[i]->readTelemetry();
        ASSERT_TRUE(t.ok());
        EXPECT_EQ(channels(t.value()), expected[i]);
    }
}

TEST(HwTelemetry, ValuesArePlausibleAndUnitsLabelled) {
    ManualClock clock;
    auto devices = discoverDevices({}, clock);
    for (const auto& d : devices) {
        ASSERT_TRUE(d->connect().ok());
    }
    for (int i = 0; i < 50; ++i) {
        auto mcu = devices[0]->readTelemetry().value();
        EXPECT_NEAR(mcu[0].value, 3.30, 0.03);
        EXPECT_EQ(mcu[0].unit, "V");
        EXPECT_EQ(mcu[1].unit, "°C");
        auto fpga = devices[1]->readTelemetry().value();
        EXPECT_EQ(fpga[1].value, 1.0); // DONE
        auto jtag = devices[2]->readTelemetry().value();
        EXPECT_NEAR(jtag[0].value, 3.29, 0.02);
        EXPECT_EQ(jtag[1].value, static_cast<double>(JtagAdapter::kSimulatedIdcode));
    }
}

TEST(HwTelemetry, NotConnectedIsAnError) {
    ManualClock clock;
    auto devices = discoverDevices({}, clock);
    auto t = devices[0]->readTelemetry();
    ASSERT_FALSE(t.ok());
    EXPECT_EQ(t.error().code, uniforge::ErrorCode::InvalidInput);
    EXPECT_FALSE(t.error().hint.empty());
}

TEST(HwTelemetry, DeterministicForASeed) {
    auto run = [](std::uint64_t seed) {
        ManualClock clock;
        DiscoveryConfig cfg;
        cfg.seed = seed;
        auto devices = discoverDevices(cfg, clock);
        std::vector<double> values;
        for (const auto& d : devices) {
            EXPECT_TRUE(d->connect().ok());
            for (int i = 0; i < 20; ++i) {
                const std::vector<Telemetry> batch = d->readTelemetry().value();
                for (const Telemetry& s : batch) {
                    values.push_back(s.value);
                }
            }
        }
        values.push_back(static_cast<double>(clock.now().count()));
        return values;
    };
    EXPECT_EQ(run(42), run(42));
    EXPECT_NE(run(42), run(43));
}

TEST(HwStateMachine, FollowsTheSpecifiedPathAndRejectsOtherEdges) {
    DeviceStateMachine sm;
    EXPECT_FALSE(sm.transition(DeviceState::Connected));
    EXPECT_FALSE(sm.transition(DeviceState::Faulted));
    EXPECT_TRUE(sm.transition(DeviceState::Connecting));
    EXPECT_TRUE(sm.transition(DeviceState::Connected));
    EXPECT_TRUE(sm.transition(DeviceState::Faulted));
    EXPECT_FALSE(sm.transition(DeviceState::Connected)); // must recover first
    EXPECT_TRUE(sm.transition(DeviceState::Recovering));
    EXPECT_TRUE(sm.transition(DeviceState::Connected));
    EXPECT_EQ(sm.history(),
              (std::vector<DeviceState>{DeviceState::Disconnected, DeviceState::Connecting,
                                        DeviceState::Connected, DeviceState::Faulted,
                                        DeviceState::Recovering, DeviceState::Connected}));
}
