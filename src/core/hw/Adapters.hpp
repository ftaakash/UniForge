#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "core/hw/MockAdapter.hpp"

namespace uniforge::hw {

// Simulated NUCLEO-F446RE seen through its ST-LINK/V2-1 (VCP data + SWD debug).
// Telemetry: vdd_V, die_temp_C. Capabilities: UART telemetry, GPIO.
class McuAdapter final : public MockAdapter {
public:
    McuAdapter(MockDeviceConfig config, IClock& clock);
    [[nodiscard]] std::vector<Capability> capabilities() const override;

protected:
    std::vector<Telemetry> sample() override;
};

// Simulated Basys 3 (Artix-7) seen through its FT2232HQ (USB-UART + USB-JTAG).
// Telemetry: xadc_temp_C, done. Capabilities: XADC temperature, DONE status.
class FpgaAdapter final : public MockAdapter {
public:
    FpgaAdapter(MockDeviceConfig config, IClock& clock);
    [[nodiscard]] std::vector<Capability> capabilities() const override;

protected:
    std::vector<Telemetry> sample() override;
};

// Simulated J-Link EDU Mini. A debug probe has no telemetry of its own: it reports what it
// senses about its target (VTref) and the IDCODE it scans from the target's debug port.
// Telemetry: vtref_V, idcode. Capabilities: IDCODE scan, VTref sense.
class JtagAdapter final : public MockAdapter {
public:
    // Simulated value only, never read from hardware (see docs/embedded_design.md §4).
    static constexpr std::uint32_t kSimulatedIdcode = 0x4BA00477u;

    JtagAdapter(MockDeviceConfig config, IClock& clock);
    [[nodiscard]] std::vector<Capability> capabilities() const override;

protected:
    std::vector<Telemetry> sample() override;
};

struct DiscoveryConfig {
    std::uint64_t seed = 20261101;
    int latencyMs = 20;
    int jitterMs = 10;
    FailMode mcuFail = FailMode::None;
    FailMode fpgaFail = FailMode::None;
    FailMode jtagFail = FailMode::None;
    RetryPolicy retry;
};

// "Discovers" the three simulated devices, in the order MCU, FPGA, JTAG probe. Each gets its own
// seed derived from config.seed. No USB enumeration or other hardware access takes place.
[[nodiscard]] std::vector<std::unique_ptr<MockAdapter>> discoverDevices(
    const DiscoveryConfig& config, IClock& clock);

} // namespace uniforge::hw
