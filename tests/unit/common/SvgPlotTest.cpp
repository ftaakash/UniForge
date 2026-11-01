// SvgPlot: deterministic output, labels and legend, reference styling, log ticks, validation.

#include "core/common/SvgPlot.hpp"

#include <limits>
#include <string>

#include <gtest/gtest.h>

using uniforge::ErrorCode;
using uniforge::PlotMarker;
using uniforge::PlotSeries;
using uniforge::PlotSpec;
using uniforge::renderSvgPlot;

namespace {

PlotSpec samplePlot() {
    PlotSpec spec;
    spec.title = "Temperature <T> & profile";
    spec.xLabel = "x (m)";
    spec.yLabel = "T (\xC2\xB0" "C)";
    PlotSeries fe;
    fe.label = "FE, N = 2";
    fe.x = {0.0, 0.5, 1.0};
    fe.y = {100.0, 185.0, 20.0};
    fe.marker = PlotMarker::Circle;
    PlotSeries exact;
    exact.label = "Exact";
    exact.x = {0.0, 0.25, 0.5, 0.75, 1.0};
    exact.y = {100.0, 173.75, 185.0, 133.75, 20.0};
    exact.color = uniforge::plotcolor::kReference;
    exact.dashed = true;
    spec.series = {fe, exact};
    return spec;
}

std::size_t count(const std::string& haystack, const std::string& needle) {
    std::size_t n = 0;
    for (std::size_t pos = haystack.find(needle); pos != std::string::npos;
         pos = haystack.find(needle, pos + 1)) {
        ++n;
    }
    return n;
}

} // namespace

TEST(SvgPlot, IsDeterministic) {
    const auto a = renderSvgPlot(samplePlot());
    const auto b = renderSvgPlot(samplePlot());
    ASSERT_TRUE(a.ok());
    ASSERT_TRUE(b.ok());
    EXPECT_EQ(a.value(), b.value());
}

TEST(SvgPlot, HasEscapedTitleAxisLabelsAndLegend) {
    const auto r = renderSvgPlot(samplePlot());
    ASSERT_TRUE(r.ok());
    const std::string& svg = r.value();
    EXPECT_EQ(svg.rfind("<?xml", 0), 0U);
    EXPECT_NE(svg.find("Temperature &lt;T&gt; &amp; profile"), std::string::npos);
    EXPECT_NE(svg.find(">x (m)</text>"), std::string::npos);
    EXPECT_NE(svg.find(">T (\xC2\xB0" "C)</text>"), std::string::npos);
    EXPECT_NE(svg.find(">FE, N = 2</text>"), std::string::npos);
    EXPECT_NE(svg.find(">Exact</text>"), std::string::npos);
    EXPECT_EQ(svg.substr(svg.size() - 7), "</svg>\n");
}

TEST(SvgPlot, ReferenceSeriesIsDashedAndDrawnLast) {
    const auto r = renderSvgPlot(samplePlot());
    ASSERT_TRUE(r.ok());
    const std::string& svg = r.value();
    // Plot line + legend sample for the dashed reference only.
    EXPECT_EQ(count(svg, "stroke-dasharray"), 2U);
    const auto fePos = svg.find("<polyline fill=\"none\" stroke=\"#D55E00\"");
    const auto exactPos = svg.find("<polyline fill=\"none\" stroke=\"#1A1E24\"");
    ASSERT_NE(fePos, std::string::npos);
    ASSERT_NE(exactPos, std::string::npos);
    EXPECT_LT(fePos, exactPos);
    EXPECT_EQ(count(svg, "<circle"), 4U) << "3 data markers + 1 legend marker";
}

TEST(SvgPlot, LogAxesUseOneTwoFiveTicks) {
    PlotSpec spec;
    spec.title = "Error";
    spec.xLabel = "h (m)";
    spec.yLabel = "error (\xC2\xB0" "C)";
    spec.logX = true;
    spec.logY = true;
    PlotSeries s;
    s.label = "measured";
    s.x = {0.1, 0.05, 0.025, 0.0125};
    s.y = {1.25, 0.3125, 0.078125, 0.01953125};
    spec.series = {s};
    const auto r = renderSvgPlot(spec);
    ASSERT_TRUE(r.ok());
    for (const char* tick : {">0.01<", ">0.02<", ">0.05<", ">0.1<", ">0.2<", ">0.5<", ">1<", ">2<"}) {
        EXPECT_NE(r.value().find(tick), std::string::npos) << tick;
    }
}

TEST(SvgPlot, WideLogAxesThinTicksToPowersOfTen) {
    // A training loss falling from 1 to 1e-20: 1-2-5 ticks would print 60+ overlapping labels.
    PlotSpec spec;
    spec.title = "Loss";
    spec.xLabel = "step";
    spec.yLabel = "J";
    spec.logY = true;
    PlotSeries s;
    s.label = "J";
    s.x = {0, 50, 100};
    s.y = {1.18, 1.3e-10, 1.7e-20};
    spec.series = {s};
    const auto r = renderSvgPlot(spec);
    ASSERT_TRUE(r.ok());
    const std::string& svg = r.value();
    for (const char* tick : {">1e-20<", ">1e-16<", ">1e-10<", ">0.0001<", ">1<"}) {
        EXPECT_NE(svg.find(tick), std::string::npos) << tick;
    }
    for (const char* tick : {">2e-20<", ">5e-10<", ">1e-19<", ">0.5<"}) {
        EXPECT_EQ(svg.find(tick), std::string::npos) << tick;
    }
}

TEST(SvgPlot, RejectsInvalidData) {
    PlotSpec empty;
    EXPECT_EQ(renderSvgPlot(empty).error().code, ErrorCode::InvalidInput);

    PlotSpec mismatched = samplePlot();
    mismatched.series[0].y.pop_back();
    EXPECT_FALSE(renderSvgPlot(mismatched).ok());

    PlotSpec nonFinite = samplePlot();
    nonFinite.series[0].y[1] = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(renderSvgPlot(nonFinite).ok());

    PlotSpec logZero = samplePlot();
    logZero.logX = true; // x = 0 cannot sit on a log axis
    EXPECT_FALSE(renderSvgPlot(logZero).ok());
}
