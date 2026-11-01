#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include "app/services/ResultStore.hpp"
#include "core/common/CancelToken.hpp"
#include "core/common/Progress.hpp"

namespace uniforge {

class LogSink;

enum class JobState { Idle, Running, Succeeded, Cancelled, Failed };

[[nodiscard]] std::string_view toString(JobState state) noexcept;

enum class SubmitResult { Accepted, Busy };

// Copy of the runner's state for the UI, taken once per frame.
struct JobSnapshot {
    std::uint64_t id = 0; // 0 = no job has run yet
    std::string name;
    JobState state = JobState::Idle;
    double fraction = 0.0; // [0, 1]
    std::string stage;
    double elapsedSeconds = 0.0; // running: so far; finished: total
    std::string message;         // outcome summary or failure reason
    bool cancelRequested = false;
};

// Thrown by job work to report an expected failure with a user-facing message.
class JobError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Handed to the work function: progress reporting and cancellation polling.
class JobContext {
public:
    using Sink = std::function<void(double fraction, std::string_view stage)>;

    JobContext(const CancelToken& cancel, Sink sink)
        : cancel_(cancel)
        , sink_(std::move(sink)) {}

    [[nodiscard]] bool cancelled() const noexcept { return cancel_.cancelled(); }
    [[nodiscard]] const CancelToken& cancelToken() const noexcept { return cancel_; }
    void report(double fraction, std::string_view stage) const;
    // Adapter for core functions that take a ProgressFn.
    [[nodiscard]] ProgressFn progressFn() const;

private:
    const CancelToken& cancel_;
    Sink sink_;
};

// Publishes a finished job's result. The work function returns it; the runner invokes it only
// when the job completed without cancellation or error.
using JobCommit = std::function<void()>;
using JobWork = std::function<JobCommit(JobContext&)>;

// Runs one background job at a time on its own thread (spec §6).
// - submit() while a job runs is rejected with SubmitResult::Busy.
// - Progress is an atomic fraction plus a stage string, read through snapshot().
// - cancel() sets the job's CancelToken; the job's commit is then discarded, so the previous
//   result in the ResultStore stays in place.
// - Exceptions thrown by the work turn into JobState::Failed; nothing is published.
class JobRunner {
public:
    explicit JobRunner(LogSink* log = nullptr);
    ~JobRunner();
    JobRunner(const JobRunner&) = delete;
    JobRunner& operator=(const JobRunner&) = delete;

    SubmitResult submit(std::string name, JobWork work);

    // Convenience form: `work(JobContext&)` returns std::shared_ptr<const T>; a non-null value
    // is published to `slot` when the job succeeds.
    template <class T, class Work>
    SubmitResult submitPublishing(std::string name, ResultSlot<T>& slot, Work work) {
        return submit(std::move(name), [&slot, work = std::move(work)](JobContext& ctx) mutable {
            std::shared_ptr<const T> value = work(ctx);
            JobCommit commit;
            if (value) {
                commit = [&slot, value]() { slot.publish(value); };
            }
            return commit;
        });
    }

    void cancel();
    [[nodiscard]] bool busy() const;
    [[nodiscard]] JobSnapshot snapshot() const;

    // Blocks until no job is running or the timeout expires. Returns true when idle.
    bool waitIdle(std::chrono::milliseconds timeout) const;

private:
    void threadMain();
    void runOne(const JobWork& work);
    void finish(JobState state, std::string message);

    LogSink* log_;
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    bool stopping_ = false;
    bool running_ = false;
    JobWork pending_;
    CancelToken cancel_;

    // Snapshot fields, guarded by mutex_ (fraction_ is atomic so report() stays cheap).
    std::uint64_t id_ = 0;
    std::string name_;
    JobState state_ = JobState::Idle;
    std::string stage_;
    std::string message_;
    std::chrono::steady_clock::time_point started_{};
    std::chrono::steady_clock::time_point finished_{};
    std::atomic<double> fraction_{0.0};

    std::thread thread_; // last member: started once everything above is initialised
};

} // namespace uniforge
