// Result<T> / Result<void> and the Error helpers (spec §5.1).

#include "core/common/Result.hpp"

#include <memory>
#include <string>

#include <gtest/gtest.h>

using uniforge::Error;
using uniforge::ErrorCode;
using uniforge::makeError;
using uniforge::Result;

namespace {

Result<double> safeDivide(double a, double b) {
    if (b == 0.0) {
        return makeError(ErrorCode::InvalidInput, "division by zero", "pass b != 0");
    }
    return a / b;
}

} // namespace

TEST(Result, HoldsValue) {
    const auto r = safeDivide(6.0, 3.0);
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(static_cast<bool>(r));
    EXPECT_DOUBLE_EQ(r.value(), 2.0);
}

TEST(Result, HoldsErrorWithMessageAndHint) {
    const auto r = safeDivide(1.0, 0.0);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(r.error().message, "division by zero");
    EXPECT_EQ(r.error().hint, "pass b != 0");
    EXPECT_FALSE(r.error().line.has_value());
    EXPECT_FALSE(r.error().retryable);
}

TEST(Result, MovesMoveOnlyValues) {
    Result<std::unique_ptr<int>> r(std::make_unique<int>(7));
    ASSERT_TRUE(r.ok());
    std::unique_ptr<int> p = std::move(r).value();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(*p, 7);
}

TEST(Result, VoidDefaultsToSuccess) {
    const Result<void> ok;
    EXPECT_TRUE(ok.ok());

    const Result<void> failed = makeError(ErrorCode::IoError, "disk full");
    ASSERT_FALSE(failed.ok());
    EXPECT_EQ(failed.error().code, ErrorCode::IoError);
}

TEST(Result, ErrorCodeNamesAreStable) {
    EXPECT_EQ(uniforge::toString(ErrorCode::InvalidInput), "InvalidInput");
    EXPECT_EQ(uniforge::toString(ErrorCode::NonFinite), "NonFinite");
    EXPECT_EQ(uniforge::toString(ErrorCode::Cancelled), "Cancelled");
    EXPECT_EQ(uniforge::toString(ErrorCode::Internal), "Internal");
}
