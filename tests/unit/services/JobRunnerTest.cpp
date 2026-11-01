// JobRunner contract (spec §6): progress, cancellation that keeps the previous result,
// busy rejection, failure containment, and the demo job's cancel latency (spec N4).

#include "app/services/JobRunner.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>

#include <gtest/gtest.h>

#include "app/services/DemoJob.hpp"
#include "app/services/LogSink.hpp"
#include "app/services/ResultStore.hpp"

using namespace std::chrono_literals;
using uniforge::JobContext;
using uniforge::JobRunner;
using uniforge::JobState;
using uniforge::ResultSlot;
using uniforge::SubmitResult;

namespace {

constexpr auto kTimeout = 5s;

// Lets a test hold a job at a known point and release it later.
class Gate {
public:
    void open() { open_.store(true); }
    void waitOpen() const {
        while (!open_.load()) {
            std::this_thread::sleep_for(1ms);
        }
    }

private:
    std::atomic<bool> open_{false};
};

// Polls until `pred` holds or the timeout expires.
template <class Pred> bool eventually(Pred pred) {
    const auto deadline = std::chrono::steady_clock::now() + kTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return pred();
}

} // namespace

TEST(JobRunner, ReportsProgressAndPublishesOnSuccess) {
    JobRunner runner;
    ResultSlot<int> slot;
    Gate gate;

    ASSERT_EQ(runner.submitPublishing("test.progress", slot,
                                      [&](JobContext& ctx) {
                                          ctx.report(0.5, "half way");
                                          gate.waitOpen();
                                          return std::make_shared<const int>(42);
                                      }),
              SubmitResult::Accepted);

    ASSERT_TRUE(eventually([&] { return runner.snapshot().stage == "half way"; }));
    const auto mid = runner.snapshot();
    EXPECT_EQ(mid.state, JobState::Running);
    EXPECT_DOUBLE_EQ(mid.fraction, 0.5);
    EXPECT_EQ(slot.get(), nullptr) << "nothing may be published before the job finishes";

    gate.open();
    ASSERT_TRUE(runner.waitIdle(kTimeout));
    const auto done = runner.snapshot();
    EXPECT_EQ(done.state, JobState::Succeeded);
    EXPECT_DOUBLE_EQ(done.fraction, 1.0);
    ASSERT_NE(slot.get(), nullptr);
    EXPECT_EQ(*slot.get(), 42);
    EXPECT_EQ(slot.generation(), 1u);
}

// spec §6 / §15 A3: jobs.cancel_keeps_previous_result
TEST(JobRunner, CancelKeepsPreviousResult) {
    JobRunner runner;
    ResultSlot<int> slot;

    runner.submitPublishing("test.first", slot,
                            [](JobContext&) { return std::make_shared<const int>(1); });
    ASSERT_TRUE(runner.waitIdle(kTimeout));
    ASSERT_EQ(*slot.get(), 1);
    const auto previous = slot.get();

    std::atomic<bool> started{false};
    runner.submitPublishing("test.cancelled", slot, [&](JobContext& ctx) {
        started = true;
        while (!ctx.cancelled()) {
            std::this_thread::sleep_for(1ms);
        }
        // A job that ignores the cancel and still returns a value must not be published.
        return std::make_shared<const int>(2);
    });
    ASSERT_TRUE(eventually([&] { return started.load(); }));
    runner.cancel();
    EXPECT_TRUE(runner.snapshot().cancelRequested);
    ASSERT_TRUE(runner.waitIdle(kTimeout));

    EXPECT_EQ(runner.snapshot().state, JobState::Cancelled);
    EXPECT_EQ(slot.get(), previous) << "the previous result object must stay in place";
    EXPECT_EQ(*slot.get(), 1);
    EXPECT_EQ(slot.generation(), 1u);
}

TEST(JobRunner, RejectsSubmitWhileBusy) {
    JobRunner runner;
    Gate gate;
    std::atomic<int> secondRan{0};

    ASSERT_EQ(runner.submit("test.long",
                            [&](JobContext&) {
                                gate.waitOpen();
                                return uniforge::JobCommit{};
                            }),
              SubmitResult::Accepted);
    const auto firstId = runner.snapshot().id;
    EXPECT_TRUE(runner.busy());

    EXPECT_EQ(runner.submit("test.second",
                            [&](JobContext&) {
                                ++secondRan;
                                return uniforge::JobCommit{};
                            }),
              SubmitResult::Busy);
    EXPECT_EQ(runner.snapshot().id, firstId) << "a rejected submit must not replace the job";
    EXPECT_EQ(runner.snapshot().name, "test.long");

    gate.open();
    ASSERT_TRUE(runner.waitIdle(kTimeout));
    EXPECT_EQ(secondRan.load(), 0);

    EXPECT_EQ(runner.submit("test.third", [](JobContext&) { return uniforge::JobCommit{}; }),
              SubmitResult::Accepted)
        << "the runner accepts work again once idle";
    ASSERT_TRUE(runner.waitIdle(kTimeout));
    EXPECT_EQ(runner.snapshot().state, JobState::Succeeded);
}

TEST(JobRunner, ExceptionFailsJobAndKeepsPreviousResult) {
    uniforge::LogSink log;
    JobRunner runner(&log);
    ResultSlot<int> slot;
    slot.publish(std::make_shared<const int>(7));

    runner.submitPublishing("test.throws", slot, [](JobContext&) -> std::shared_ptr<const int> {
        throw uniforge::JobError("input rejected");
    });
    ASSERT_TRUE(runner.waitIdle(kTimeout));

    const auto s = runner.snapshot();
    EXPECT_EQ(s.state, JobState::Failed);
    EXPECT_EQ(s.message, "input rejected");
    EXPECT_EQ(*slot.get(), 7);
    EXPECT_EQ(log.snapshot().back().level, uniforge::LogLevel::Error);
}

TEST(JobRunner, DestructorCancelsRunningJob) {
    std::atomic<bool> started{false};
    std::atomic<bool> sawCancel{false};
    {
        JobRunner runner;
        runner.submit("test.shutdown", [&](JobContext& ctx) {
            started = true;
            while (!ctx.cancelled()) {
                std::this_thread::sleep_for(1ms);
            }
            sawCancel = true;
            return uniforge::JobCommit{};
        });
        ASSERT_TRUE(eventually([&] { return started.load(); }));
    } // ~JobRunner must cancel and join instead of hanging
    EXPECT_TRUE(sawCancel.load());
}

TEST(DemoJob, CompletesAndCountsSteps) {
    JobRunner runner;
    uniforge::ResultStore store;
    const uniforge::DemoJobConfig cfg{3, 5ms};

    runner.submitPublishing("demo.steps", store.demo,
                            [&](JobContext& ctx) { return uniforge::runDemoJob(ctx, cfg, 1); });
    ASSERT_TRUE(runner.waitIdle(kTimeout));

    const auto result = store.demo.get();
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->run, 1u);
    EXPECT_EQ(result->steps, 3);
    EXPECT_GE(result->seconds, 0.015);
    EXPECT_EQ(runner.snapshot().stage, "Done");
}

// spec N4: cancel latency <= 200 ms.
TEST(DemoJob, CancelLatencyIsUnder200ms) {
    JobRunner runner;
    uniforge::ResultStore store;
    const uniforge::DemoJobConfig cfg{50, 100ms}; // 5 s if not cancelled

    runner.submitPublishing("demo.steps", store.demo,
                            [&](JobContext& ctx) { return uniforge::runDemoJob(ctx, cfg, 1); });
    ASSERT_TRUE(eventually([&] { return runner.snapshot().stage == "Step 2/50"; }));

    const auto t0 = std::chrono::steady_clock::now();
    runner.cancel();
    ASSERT_TRUE(runner.waitIdle(kTimeout));
    const auto latency = std::chrono::steady_clock::now() - t0;

    EXPECT_LT(latency, 200ms);
    EXPECT_EQ(runner.snapshot().state, JobState::Cancelled);
    EXPECT_EQ(store.demo.get(), nullptr);
}
