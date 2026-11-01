#include "core/hw/DeviceStateMachine.hpp"

namespace uniforge::hw {

bool DeviceStateMachine::allowed(DeviceState from, DeviceState to) noexcept {
    using S = DeviceState;
    switch (from) {
    case S::Disconnected:
        return to == S::Connecting;
    case S::Connecting:
        return to == S::Connected || to == S::Faulted;
    case S::Connected:
        return to == S::Faulted || to == S::Disconnected;
    case S::Faulted:
        return to == S::Recovering || to == S::Disconnected;
    case S::Recovering:
        return to == S::Connected || to == S::Faulted;
    }
    return false;
}

bool DeviceStateMachine::transition(DeviceState to) {
    if (!allowed(state_, to)) {
        return false;
    }
    state_ = to;
    if (history_.size() == kHistoryLimit) {
        history_.erase(history_.begin());
    }
    history_.push_back(to);
    return true;
}

} // namespace uniforge::hw
