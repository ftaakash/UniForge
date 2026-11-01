#include "app/services/JobRunner.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <utility>

#include "app/services/LogSink.hpp"

namespace uniforge {

namespace {

std::string formatDuration(double seconds) {
    char buf[32];
    if (seconds < 1.0) {
        std::snprintf(buf, sizeof buf, "%.1f ms", seconds * 1e3);
    } else {
        std::snprintf(buf, sizeof buf, "%.2f s", seconds);
    }
    return buf;
}

double secondsBetween(std::chrono::steady_clock::time_point a,
                      std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}

} // namespace

std::string_view toString(JobState state) noexcept {
    switch (state) {
    case JobState::Idle:
        return "Idle";
    case JobState::Running:
        return "Running";
    case JobState::Succeeded:
        return "Succeeded";
    case JobState::Cancelled:
        return "Cancelled";
    case JobState::Failed:
        return "Failed";
    }
    return "Idle";
}

void JobContext::report(double fraction, std::string_view stage) const {
    if (sink_) {
        sink_(fraction, stage);
    }
}

ProgressFn JobContext::progressFn() const {
    return [this](double fraction, std::string_view stage) { report(fraction, stage); };
}

JobRunner::JobRunner(LogSink* log)
    : log_(log)
    , thread_([this] { threadMain(); }) {}

JobRunner::~JobRunner() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        cancel_.requestCancel();
    }
    cv_.notify_all();
    thread_.join();
}

SubmitResult JobRunner::submit(std::string name, JobWork work) {
    std::uint64_t id = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_ || stopping_) {
            if (log_) {
                log_->warn("job " + name + " rejected: busy with job=" + std::to_string(id_) + " " +
                           name_);
            }
            return SubmitResult::Busy;
        }
        running_ = true;
        pending_ = std::move(work);
        id = ++id_;
        name_ = name;
        state_ = JobState::Running;
        stage_ = "Starting";
        message_.clear();
        started_ = std::chrono::steady_clock::now();
        finished_ = started_;
        cancel_.reset();
        fraction_.store(0.0, std::memory_order_relaxed);
    }
    cv_.notify_all();
    if (log_) {
        log_->info("job=" + std::to_string(id) + " " + name + " started");
    }
    return SubmitResult::Accepted;
}

void JobRunner::cancel() {
    std::uint64_t id = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ || cancel_.cancelled()) {
            return;
        }
        cancel_.requestCancel();
        id = id_;
    }
    if (log_) {
        log_->info("job=" + std::to_string(id) + " cancel requested");
    }
}

bool JobRunner::busy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

JobSnapshot JobRunner::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    JobSnapshot s;
    s.id = id_;
    s.name = name_;
    s.state = state_;
    s.fraction = fraction_.load(std::memory_order_relaxed);
    s.stage = stage_;
    s.message = message_;
    s.cancelRequested = running_ && cancel_.cancelled();
    if (id_ != 0) {
        s.elapsedSeconds =
            secondsBetween(started_, running_ ? std::chrono::steady_clock::now() : finished_);
    }
    return s;
}

bool JobRunner::waitIdle(std::chrono::milliseconds timeout) const {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this] { return !running_; });
}

void JobRunner::threadMain() {
    for (;;) {
        JobWork work;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || pending_; });
            if (stopping_) {
                return;
            }
            work = std::move(pending_);
            pending_ = nullptr;
        }
        runOne(work);
    }
}

void JobRunner::runOne(const JobWork& work) {
    JobContext ctx(cancel_, [this](double fraction, std::string_view stage) {
        fraction_.store(std::clamp(fraction, 0.0, 1.0), std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(mutex_);
        stage_.assign(stage.data(), stage.size());
    });

    // Work and commit run here, on the runner thread. Any exception (including bad_alloc) is
    // contained at this boundary and reported; nothing is published in that case.
    try {
        JobCommit commit = work(ctx);
        if (cancel_.cancelled()) {
            finish(JobState::Cancelled, "Cancelled — previous results kept");
            return;
        }
        if (commit) {
            commit();
        }
    } catch (const std::exception& e) {
        finish(JobState::Failed, e.what());
        return;
    } catch (...) {
        finish(JobState::Failed, "Unexpected internal error");
        return;
    }
    fraction_.store(1.0, std::memory_order_relaxed);
    finish(JobState::Succeeded, "Done");
}

void JobRunner::finish(JobState state, std::string message) {
    const auto finished = std::chrono::steady_clock::now();
    std::string prefix;
    double seconds = 0.0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        prefix = "job=" + std::to_string(id_) + " " + name_ + " ";
        seconds = secondsBetween(started_, finished);
    }
    if (state == JobState::Succeeded) {
        message = "Done in " + formatDuration(seconds);
    }

    // Log before marking the runner idle, so whoever observes the idle state (UI, tests,
    // shutdown) also finds the job's final log line.
    if (log_) {
        switch (state) {
        case JobState::Succeeded:
            log_->info(prefix + "done " + formatDuration(seconds));
            break;
        case JobState::Cancelled:
            log_->info(prefix + "cancelled after " + formatDuration(seconds) +
                       ", previous result kept");
            break;
        default:
            log_->error(prefix + "failed after " + formatDuration(seconds) + ": " + message);
            break;
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        finished_ = finished;
        state_ = state;
        message_ = std::move(message);
        running_ = false;
    }
    cv_.notify_all();
}

} // namespace uniforge
