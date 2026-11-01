// DeviceManager (spec §6): commands go through the queue, the 10 Hz tick fills the telemetry
// ring buffers, recovery is logged, and destruction does not hang on a simulated timeout.

#include "app/services/DeviceManager.hpp"

#include <chrono>
#include <functional>
#include <thread>

#include <gtest/gtest.h>

#include "app/services/LogSink.hpp"

using namespace std::chrono_literals;
using uniforge::DeviceManager;
using uniforge::HardwareSnapshot;
using uniforge::LogSink;
using uniforge::hw::DeviceState;

namespace {

bool waitFor(const DeviceManager& m, const std::function<bool(const HardwareSnapshot&)>& pred,
             std::chrono::milliseconds timeout = 5s) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
        if (pred(m.snapshot())) {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return false;
}

bool logContains(const LogSink& log, const std::string& needle) {
    for (const auto& e : log.snapshot()) {
        if (e.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(DeviceManager, DiscoverConnectAndReadTelemetry) {
    LogSink log;
    DeviceManager m(&log, 20ms);
    m.post(DeviceManager::Command::Discover);
    ASSERT_TRUE(waitFor(m, [](const HardwareSnapshot& s) { return s.devices.size() == 3; }));
    for (const auto& d : m.snapshot().devices) {
        EXPECT_TRUE(d.info.simulated);
    }
    m.post(DeviceManager::Command::Connect, 0);
    ASSERT_TRUE(waitFor(m, [](const HardwareSnapshot& s) {
        return s.devices[0].state == DeviceState::Connected && s.devices[0].history.size() == 2 &&
               s.devices[0].history[0].size() >= 3;
    }));
    EXPECT_EQ(m.snapshot().devices[0].latest[0].channel, "vdd_V");
    EXPECT_TRUE(logContains(log, "simulated devices (no hardware accessed)"));
}

TEST(DeviceManager, InjectedTimeoutIsRecoveredAndLogged) {
    LogSink log;
    DeviceManager m(&log, 20ms);
    m.post(DeviceManager::Command::Discover);
    m.post(DeviceManager::Command::Connect, 1);
    ASSERT_TRUE(waitFor(m, [](const HardwareSnapshot& s) {
        return s.devices.size() == 3 && s.devices[1].state == DeviceState::Connected;
    }));
    m.post(DeviceManager::Command::InjectTimeout, 1);
    ASSERT_TRUE(waitFor(m, [](const HardwareSnapshot& s) {
        return s.devices[1].recoverySummary == "recovered after 1 retry";
    }));
    EXPECT_TRUE(logContains(log, "hw SIM-FPGA-0001: recovered after 1 retry"));
}

TEST(DeviceManager, ShutsDownPromptlyDuringRetries) {
    LogSink log;
    auto m = std::make_unique<DeviceManager>(&log, 20ms);
    m->post(DeviceManager::Command::Discover);
    m->post(DeviceManager::Command::Connect, 2);
    ASSERT_TRUE(waitFor(*m, [](const HardwareSnapshot& s) {
        return s.devices.size() == 3 && s.devices[2].state == DeviceState::Connected;
    }));
    m->post(DeviceManager::Command::InjectTimeout, 2);
    std::this_thread::sleep_for(60ms); // inside the 250 ms simulated timeout
    const auto t0 = std::chrono::steady_clock::now();
    m.reset();
    EXPECT_LT(std::chrono::steady_clock::now() - t0, 500ms);
}
