#pragma once

#include <string>
#include <vector>

#include "core/common/Result.hpp"
#include "core/hw/DeviceTypes.hpp"

namespace uniforge::hw {

// One attached tool as the workbench sees it (spec §5.2, §7.5). Implementations are not
// thread-safe; the DeviceManager thread owns them.
//
// Contract:
//  - connect(): Disconnected → Connected, or an Error (DeviceTimeout is retryable). Calling it
//    while already Connected is a no-op success.
//  - disconnect(): → Disconnected from Connected or Faulted; a no-op success when already
//    Disconnected.
//  - readTelemetry(): one telemetry message (one sample per channel) while Connected.
//  - isSimulated(): true for every adapter in UniForge; no hardware is ever accessed.
class IDeviceAdapter {
public:
    virtual ~IDeviceAdapter() = default;

    [[nodiscard]] virtual const DeviceInfo& info() const = 0;
    [[nodiscard]] virtual std::vector<Capability> capabilities() const = 0;
    [[nodiscard]] virtual DeviceState state() const = 0;
    [[nodiscard]] virtual bool isSimulated() const = 0;

    virtual Result<void> connect() = 0;
    virtual Result<void> disconnect() = 0;
    virtual Result<std::vector<Telemetry>> readTelemetry() = 0;
};

} // namespace uniforge::hw
