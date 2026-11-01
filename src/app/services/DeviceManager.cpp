#include "app/services/DeviceManager.hpp"

#include <utility>

#include "app/services/LogSink.hpp"

namespace uniforge {

DeviceManager::DeviceManager(LogSink* log, std::chrono::milliseconds tick)
    : log_(log)
    , tickPeriod_(tick)
    , thread_([this] { threadMain(); }) {}

DeviceManager::~DeviceManager() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    clock_.interrupt(); // cut short any simulated timeout or back-off in progress
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void DeviceManager::post(Command command, int device) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back({command, device});
    }
    cv_.notify_all();
}

HardwareSnapshot DeviceManager::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

void DeviceManager::threadMain() {
    auto nextTick = std::chrono::steady_clock::now() + tickPeriod_;
    for (;;) {
        std::deque<Pending> work;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_until(lock, nextTick, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) {
                return;
            }
            work.swap(queue_);
        }
        busy_.store(true, std::memory_order_release);
        for (const Pending& p : work) {
            execute(p);
            publish(); // show each command's outcome before the next one
        }
        if (std::chrono::steady_clock::now() >= nextTick) {
            tick();
            publish();
            nextTick = std::chrono::steady_clock::now() + tickPeriod_;
        }
        busy_.store(false, std::memory_order_release);
    }
}

void DeviceManager::execute(const Pending& p) {
    if (p.command == Command::Discover) {
        for (auto& d : devices_) {
            (void)d->disconnect();
        }
        hw::DiscoveryConfig cfg;
        cfg.latencyMs = 20;
        cfg.jitterMs = 10;
        devices_ = hw::discoverDevices(cfg, clock_);
        latest_.assign(devices_.size(), {});
        history_.assign(devices_.size(), {});
        for (auto& d : devices_) {
            d->setEventSink([this](hw::EventLevel level, const std::string& text) {
                log(level, text);
            });
        }
        log(hw::EventLevel::Info, "discovered " + std::to_string(devices_.size()) +
                                      " simulated devices (no hardware accessed)");
        return;
    }
    if (p.device < 0 || static_cast<std::size_t>(p.device) >= devices_.size()) {
        return;
    }
    hw::MockAdapter& d = *devices_[static_cast<std::size_t>(p.device)];
    Result<void> r;
    switch (p.command) {
    case Command::Connect:
        r = d.connect();
        break;
    case Command::Disconnect:
        r = d.disconnect();
        break;
    case Command::InjectTimeout:
        d.injectTimeoutOnce();
        log(hw::EventLevel::Info, d.info().id + ": timeout injected for the next request");
        break;
    case Command::Retry:
        r = d.retry();
        break;
    case Command::ResetFault:
        r = d.resetFault();
        break;
    case Command::Discover:
        break;
    }
    // Device faults are already logged through the event sink; report refused actions here.
    if (!r.ok() && r.error().code == ErrorCode::InvalidInput) {
        log(hw::EventLevel::Warn, r.error().message + " " + r.error().hint);
    }
}

void DeviceManager::tick() {
    for (std::size_t i = 0; i < devices_.size(); ++i) {
        hw::MockAdapter& d = *devices_[i];
        if (d.state() != hw::DeviceState::Connected) {
            continue;
        }
        auto t = d.readTelemetry();
        if (!t.ok()) {
            continue; // the adapter is Faulted now and has logged why
        }
        latest_[i] = std::move(t).value();
        auto& channels = history_[i].values;
        channels.resize(latest_[i].size());
        for (std::size_t c = 0; c < latest_[i].size(); ++c) {
            channels[c].push_back(static_cast<float>(latest_[i][c].value));
            if (channels[c].size() > kHistoryCapacity) {
                channels[c].pop_front();
            }
        }
    }
}

void DeviceManager::publish() {
    HardwareSnapshot s;
    s.discovered = !devices_.empty();
    for (std::size_t i = 0; i < devices_.size(); ++i) {
        const hw::MockAdapter& d = *devices_[i];
        DeviceSnapshot ds;
        ds.info = d.info();
        ds.capabilities = d.capabilities();
        ds.state = d.state();
        ds.latest = latest_[i];
        for (const auto& ch : history_[i].values) {
            ds.history.emplace_back(ch.begin(), ch.end());
        }
        ds.lastError = d.lastError();
        ds.recoverySummary = d.lastRecoverySummary();
        ds.retryTrail = d.retryTrail();
        s.devices.push_back(std::move(ds));
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = std::move(s);
    }
    version_.fetch_add(1, std::memory_order_acq_rel);
}

void DeviceManager::log(hw::EventLevel level, const std::string& text) {
    if (log_ == nullptr) {
        return;
    }
    std::string line = "hw " + text;
    switch (level) {
    case hw::EventLevel::Info:
        log_->info(std::move(line));
        break;
    case hw::EventLevel::Warn:
        log_->warn(std::move(line));
        break;
    case hw::EventLevel::Error:
        log_->error(std::move(line));
        break;
    }
}

} // namespace uniforge
