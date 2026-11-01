#pragma once

#include <memory>

#include "app/views/View.hpp"

namespace uniforge {
class DeviceManager;
struct DeviceSnapshot;
} // namespace uniforge

namespace uniforge::views {

// Hardware (Task E, spec §8.6): simulated banner, Discover, one DeviceCard per simulated device
// (state, capabilities, telemetry, connect/disconnect, fault injection and recovery), and a
// link to the embedded design notes. Owns the DeviceManager, started on first display.
class HardwareView final : public View {
public:
    HardwareView();
    ~HardwareView() override;

    [[nodiscard]] const char* title() const override { return "Hardware"; }
    [[nodiscard]] const char* tag() const override { return "Task E"; }
    [[nodiscard]] const char* purpose() const override {
        return "Simulated MCU, FPGA and JTAG adapters: discovery, connect, telemetry and "
               "recovery.";
    }
    void draw(AppContext& ctx) override;

private:
    void drawDeviceCard(int index, const DeviceSnapshot& d, bool busy);
    void openDesignNotes(AppContext& ctx);

    std::unique_ptr<DeviceManager> devices_;
};

} // namespace uniforge::views
