#include "core/hw/Adapters.hpp"

#include <cmath>

namespace uniforge::hw {

namespace {

// Synthetic values, chosen to look plausible on screen. They are not measurements.
constexpr double kVddNominal_V = 3.30;
constexpr double kMcuTempBase_C = 31.0;
constexpr double kFpgaTempBase_C = 38.0;
constexpr double kVtrefNominal_V = 3.29;

DeviceInfo mcuInfo() {
    return {"SIM-MCU-0001", "NUCLEO-F446RE (STM32F446RE)",
            "ST-LINK/V2-1 USB: VCP data + SWD debug (simulated)", DeviceKind::Microcontroller,
            true};
}

DeviceInfo fpgaInfo() {
    return {"SIM-FPGA-0001", "Basys 3 (Artix-7 XC7A35T)",
            "FT2232HQ USB: UART data + USB-JTAG (simulated)", DeviceKind::Fpga, true};
}

DeviceInfo jtagInfo() {
    return {"SIM-JTAG-0001", "SEGGER J-Link EDU Mini",
            "USB to 9-pin SWD/JTAG on the MCU target (simulated)", DeviceKind::JtagProbe, true};
}

} // namespace

McuAdapter::McuAdapter(MockDeviceConfig config, IClock& clock)
    : MockAdapter(mcuInfo(), config, clock) {}

std::vector<Capability> McuAdapter::capabilities() const {
    return {{"UART telemetry", "Reads VDD and die temperature over the ST-LINK virtual COM port"},
            {"GPIO", "Drives and reads general-purpose pins (3.3 V, FT pins 5 V tolerant)"}};
}

std::vector<Telemetry> McuAdapter::sample() {
    const double t = static_cast<double>(samplesTaken());
    const double vdd = kVddNominal_V + (uniform() - 0.5) * 0.04;
    const double temp = kMcuTempBase_C + 1.5 * std::sin(t / 40.0) + (uniform() - 0.5) * 0.6;
    return {make("vdd_V", vdd, "V"), make("die_temp_C", temp, "°C")};
}

FpgaAdapter::FpgaAdapter(MockDeviceConfig config, IClock& clock)
    : MockAdapter(fpgaInfo(), config, clock) {}

std::vector<Capability> FpgaAdapter::capabilities() const {
    return {{"XADC temperature", "On-chip temperature from the Artix-7 XADC"},
            {"DONE status", "Configuration DONE flag (1 = configured)"}};
}

std::vector<Telemetry> FpgaAdapter::sample() {
    const double t = static_cast<double>(samplesTaken());
    const double temp = kFpgaTempBase_C + 2.0 * std::sin(t / 55.0) + (uniform() - 0.5) * 0.4;
    return {make("xadc_temp_C", temp, "°C"), make("done", 1.0, "")};
}

JtagAdapter::JtagAdapter(MockDeviceConfig config, IClock& clock)
    : MockAdapter(jtagInfo(), config, clock) {}

std::vector<Capability> JtagAdapter::capabilities() const {
    return {{"IDCODE scan", "Reads the target debug port's IDCODE (debug probe, not a compute "
                            "board)"},
            {"VTref sense", "Senses the target I/O voltage before driving any signal"}};
}

std::vector<Telemetry> JtagAdapter::sample() {
    const double vtref = kVtrefNominal_V + (uniform() - 0.5) * 0.02;
    return {make("vtref_V", vtref, "V"),
            make("idcode", static_cast<double>(kSimulatedIdcode), "")};
}

std::vector<std::unique_ptr<MockAdapter>> discoverDevices(const DiscoveryConfig& config,
                                                          IClock& clock) {
    auto cfg = [&](std::uint64_t index, FailMode fail) {
        MockDeviceConfig c;
        c.transport.seed = config.seed * 6364136223846793005ull + index;
        c.transport.latencyMs = config.latencyMs;
        c.transport.jitterMs = config.jitterMs;
        c.transport.failMode = fail;
        c.retry = config.retry;
        return c;
    };
    std::vector<std::unique_ptr<MockAdapter>> out;
    out.push_back(std::make_unique<McuAdapter>(cfg(1, config.mcuFail), clock));
    out.push_back(std::make_unique<FpgaAdapter>(cfg(2, config.fpgaFail), clock));
    out.push_back(std::make_unique<JtagAdapter>(cfg(3, config.jtagFail), clock));
    return out;
}

} // namespace uniforge::hw
