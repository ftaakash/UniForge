#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace uniforge::hw {

// Device data model (spec §5.3). Every adapter in UniForge is a software simulation, so
// DeviceInfo::simulated defaults to true and the tests assert it for every adapter.
enum class DeviceKind { Microcontroller, Fpga, JtagProbe };

// Lifecycle (spec §7.5): Disconnected → Connecting → Connected → Faulted → Recovering →
// Connected. DeviceStateMachine enforces the allowed edges.
enum class DeviceState { Disconnected, Connecting, Connected, Faulted, Recovering };

struct DeviceInfo {
    std::string id;        // simulated identifier, e.g. "SIM-MCU-0001"
    std::string model;     // board the mock stands in for
    std::string transport; // host link the real board would use
    DeviceKind kind = DeviceKind::Microcontroller;
    bool simulated = true;
};

struct Capability {
    std::string name;
    std::string description;
};

struct Telemetry {
    std::chrono::system_clock::time_point t;
    std::string channel; // e.g. "vdd_V"
    double value = 0.0;
    std::string unit; // "V", "°C", "" for flags and identifiers
};

[[nodiscard]] constexpr std::string_view toString(DeviceKind kind) noexcept {
    switch (kind) {
    case DeviceKind::Microcontroller:
        return "MCU";
    case DeviceKind::Fpga:
        return "FPGA";
    case DeviceKind::JtagProbe:
        return "JTAG";
    }
    return "Unknown";
}

[[nodiscard]] constexpr std::string_view toString(DeviceState state) noexcept {
    switch (state) {
    case DeviceState::Disconnected:
        return "Disconnected";
    case DeviceState::Connecting:
        return "Connecting";
    case DeviceState::Connected:
        return "Connected";
    case DeviceState::Faulted:
        return "Faulted";
    case DeviceState::Recovering:
        return "Recovering";
    }
    return "Unknown";
}

} // namespace uniforge::hw
