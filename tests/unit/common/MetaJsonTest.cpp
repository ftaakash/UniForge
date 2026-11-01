// MetaJson sidecar writer and the UTC timestamp formatter.

#include "core/common/MetaJson.hpp"

#include <chrono>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using uniforge::formatUtcIso8601;
using uniforge::MetaJson;

TEST(MetaJson, KeepsOrderNestsAndEscapes) {
    MetaJson params;
    params.set("L_m", 1.0).set("elements", 10);
    MetaJson meta;
    meta.set("tool", "uniforge_cli")
        .set("quote", "say \"hi\"\\")
        .set("params", params)
        .set("columns", std::vector<std::string>{"x_m", "T_fe_C"})
        .set("N", std::vector<int>{10, 20})
        .set("ok", true)
        .set("nan", std::numeric_limits<double>::quiet_NaN());
    EXPECT_EQ(meta.str(), "{\n"
                          "  \"tool\": \"uniforge_cli\",\n"
                          "  \"quote\": \"say \\\"hi\\\"\\\\\",\n"
                          "  \"params\": {\n"
                          "    \"L_m\": 1,\n"
                          "    \"elements\": 10\n"
                          "  },\n"
                          "  \"columns\": [\"x_m\", \"T_fe_C\"],\n"
                          "  \"N\": [10, 20],\n"
                          "  \"ok\": true,\n"
                          "  \"nan\": null\n"
                          "}\n");
}

TEST(MetaJson, EmptyObject) { EXPECT_EQ(MetaJson().str(), "{}\n"); }

TEST(MetaJson, FormatsUtcTimestamps) {
    using std::chrono::seconds;
    using std::chrono::system_clock;
    EXPECT_EQ(formatUtcIso8601(system_clock::time_point{}), "1970-01-01T00:00:00Z");
    // 2026-11-01T10:22:04Z = 1793528524 s after the epoch.
    EXPECT_EQ(formatUtcIso8601(system_clock::time_point{seconds{1793528524}}),
              "2026-11-01T10:22:04Z");
    // Leap day.
    EXPECT_EQ(formatUtcIso8601(system_clock::time_point{seconds{951782400}}),
              "2000-02-29T00:00:00Z");
}
