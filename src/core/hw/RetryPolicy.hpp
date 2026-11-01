#pragma once

#include <array>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "core/common/Result.hpp"
#include "core/hw/Clock.hpp"

namespace uniforge::hw {

// Recovery rule from spec §7.5: each request has a 250 ms timeout; a retryable failure is
// retried up to 3 times after 100, 200 and 400 ms of back-off.
struct RetryPolicy {
    std::chrono::milliseconds timeout{250};
    std::array<std::chrono::milliseconds, 3> backoff{std::chrono::milliseconds(100),
                                                     std::chrono::milliseconds(200),
                                                     std::chrono::milliseconds(400)};
    [[nodiscard]] int maxRetries() const noexcept { return static_cast<int>(backoff.size()); }
};

// One retry, for the retry trail shown on the DeviceCard.
struct RetryAttempt {
    int retry = 0; // 1-based
    std::chrono::milliseconds backoff{0};
    bool ok = false;
    std::string detail; // error message, or "ok"
};

struct RetryOutcome {
    Result<void> result;  // last attempt's result; on exhaustion carries the final hint
    int retries = 0;      // retries actually made (0..maxRetries)
    std::vector<RetryAttempt> trail;
    std::string summary;  // "recovered after 1 retry", "failed after 3 retries", ...
};

// Retries `op` (called with the per-request timeout) according to `policy`, sleeping the
// back-off on `clock` before each retry. Does not call `op` for the first try: the caller has
// already seen `firstError`. Non-retryable errors stop immediately. On exhaustion the error's
// hint is replaced by a summary of what was tried and what to do next.
[[nodiscard]] RetryOutcome retryAfterFailure(
    const Error& firstError, const RetryPolicy& policy, IClock& clock,
    const std::function<Result<void>(std::chrono::milliseconds)>& op);

} // namespace uniforge::hw
