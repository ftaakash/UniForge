#pragma once

#include <cstddef>
#include <vector>

#include "core/hw/DeviceTypes.hpp"

namespace uniforge::hw {

// Device lifecycle with the allowed edges only (spec §7.5):
//
//   Disconnected → Connecting → Connected ──► Faulted ──► Recovering ──► Connected
//                       │            │           ▲   │          │
//                       └──► Faulted └► Disc.    │   └► Disc.   └──► Faulted (retries exhausted)
//
// A failed request moves the device to Faulted; recovery (automatic retries, or the user's
// Retry) runs in Recovering and ends in Connected or back in Faulted. Disconnect is allowed from
// Connected and Faulted. Anything else is rejected and leaves the state unchanged.
class DeviceStateMachine {
public:
    [[nodiscard]] DeviceState state() const noexcept { return state_; }
    [[nodiscard]] static bool allowed(DeviceState from, DeviceState to) noexcept;

    // Moves to `to` when the edge is allowed. Returns false (state unchanged) otherwise.
    bool transition(DeviceState to);

    // Every state entered, starting with Disconnected. Bounded to the last 64 entries.
    [[nodiscard]] const std::vector<DeviceState>& history() const noexcept { return history_; }

private:
    static constexpr std::size_t kHistoryLimit = 64;
    DeviceState state_ = DeviceState::Disconnected;
    std::vector<DeviceState> history_{DeviceState::Disconnected};
};

} // namespace uniforge::hw
