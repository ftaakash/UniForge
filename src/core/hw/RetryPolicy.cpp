#include "core/hw/RetryPolicy.hpp"

#include <utility>

namespace uniforge::hw {

namespace {

std::string retries(int n) {
    return std::to_string(n) + (n == 1 ? " retry" : " retries");
}

std::string backoffList(const RetryPolicy& policy) {
    std::string s;
    for (const std::chrono::milliseconds b : policy.backoff) {
        s += (s.empty() ? "" : "/") + std::to_string(b.count());
    }
    return s + " ms";
}

} // namespace

RetryOutcome retryAfterFailure(const Error& firstError, const RetryPolicy& policy, IClock& clock,
                               const std::function<Result<void>(std::chrono::milliseconds)>& op) {
    RetryOutcome out{Result<void>(firstError), 0, {}, {}};
    if (!firstError.retryable) {
        out.summary = "not retried (" + std::string(toString(firstError.code)) + ")";
        return out;
    }
    for (int i = 0; i < policy.maxRetries(); ++i) {
        const std::chrono::milliseconds wait = policy.backoff[static_cast<std::size_t>(i)];
        clock.sleepFor(wait);
        Result<void> r = op(policy.timeout);
        out.retries = i + 1;
        RetryAttempt attempt{i + 1, wait, r.ok(), r.ok() ? std::string("ok") : r.error().message};
        out.trail.push_back(std::move(attempt));
        if (r.ok()) {
            out.result = Result<void>();
            out.summary = "recovered after " + retries(out.retries);
            return out;
        }
        const bool retryable = r.error().retryable;
        out.result = std::move(r);
        if (!retryable) {
            break;
        }
    }
    Error last = out.result.error();
    last.hint = "Gave up after " + retries(out.retries) + " (" +
                std::to_string(policy.timeout.count()) + " ms timeout, back-off " + backoffList(policy) + "). " +
                last.hint;
    if (last.code != ErrorCode::DeviceTimeout) {
        last.code = ErrorCode::DeviceFault;
    }
    out.result = Result<void>(std::move(last));
    out.summary = "failed after " + retries(out.retries);
    return out;
}

} // namespace uniforge::hw
