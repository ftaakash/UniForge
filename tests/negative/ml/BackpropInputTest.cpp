// Invalid Task F inputs are rejected with a typed error that names the value (spec §5.2).

#include "core/ml/TinyNet.hpp"

#include <limits>

#include <gtest/gtest.h>

using namespace uniforge;
using namespace uniforge::ml;

TEST(BackpropInput, BriefProblemIsValid) {
    EXPECT_TRUE(BackpropProblem{}.validate().ok());
}

TEST(BackpropInput, RejectsNonFiniteValues) {
    BackpropProblem p;
    p.init.v = std::numeric_limits<double>::infinity();
    const auto r = p.validate();
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::NonFinite);
    EXPECT_NE(r.error().message.find("Weight v"), std::string::npos);

    BackpropProblem q;
    q.x = std::numeric_limits<double>::quiet_NaN();
    ASSERT_FALSE(q.validate().ok());
    EXPECT_EQ(q.validate().error().code, ErrorCode::NonFinite);
}

TEST(BackpropInput, RejectsNonPositiveLearningRate) {
    for (double lr : {0.0, -0.1}) {
        BackpropProblem p;
        p.lr = lr;
        const auto r = p.validate();
        ASSERT_FALSE(r.ok());
        EXPECT_EQ(r.error().code, ErrorCode::InvalidInput);
        EXPECT_FALSE(r.error().hint.empty());
    }
}

TEST(BackpropInput, RejectsStepCountsOutOfRange) {
    BackpropProblem p;
    p.steps = 0;
    ASSERT_FALSE(p.validate().ok());
    EXPECT_EQ(p.validate().error().code, ErrorCode::InvalidInput);
    p.steps = BackpropProblem::kMaxSteps + 1;
    ASSERT_FALSE(p.validate().ok());
    EXPECT_EQ(p.validate().error().code, ErrorCode::LimitExceeded);
}
