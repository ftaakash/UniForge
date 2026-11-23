#include <cmath>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/common/SvgPlot.hpp"
#include "core/viz/Colormap.hpp"
#include "core/viz/Image.hpp"
#include "core/viz/Raster3D.hpp"
#include "core/viz/Spy.hpp"
#include "core/viz/SvgCanvas.hpp"

namespace uniforge::viz {
namespace {

// Two triangles forming the square [x0, x1] × [y0, y1] at height z, facing +z.
TriangleMesh square(double x0, double x1, double y0, double y1, double z, const Rgb& c) {
    TriangleMesh m;
    m.positions = {{x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}};
    m.triangles = {{0, 1, 2}, {0, 2, 3}};
    m.triangleColor = {c, c};
    m.shaded = false;
    return m;
}

ViewSpec topView() {
    ViewSpec v;
    v.azimuthDeg = -90.0;  // right = +x
    v.elevationDeg = 90.0; // looking down −z
    v.width = 200;
    v.height = 200;
    v.supersample = 2;
    v.margin = 0.0;
    return v;
}

TEST(Viz, HexColorRoundTrips) {
    EXPECT_EQ(hexColor("#0072B2"), (Rgb{0x00, 0x72, 0xB2}));
    EXPECT_EQ(toHex({0xE6, 0x9F, 0x00}), "#E69F00");
    EXPECT_EQ(hexColor("bad"), (Rgb{0, 0, 0}));
}

TEST(Viz, InfernoRunsFromDarkToBright) {
    const Rgb lo = inferno(0.0), hi = inferno(1.0);
    EXPECT_LT(lo[0] + lo[1] + lo[2], 30);
    EXPECT_GT(hi[0] + hi[1] + hi[2], 600);
    EXPECT_EQ(inferno(-1.0), lo); // clamped
    EXPECT_EQ(inferno(50.0, 0.0, 100.0), inferno(0.5));
    EXPECT_NE(categorical(0), categorical(1));
}

TEST(Viz, DownsampleAveragesBlocks) {
    Image img(4, 2, {0, 0, 0});
    img.fillRect(0, 0, 1, 2, {200, 100, 0}); // left column of the first 2×2 block
    const Image d = img.downsample(2);
    ASSERT_EQ(d.width, 2);
    ASSERT_EQ(d.height, 1);
    EXPECT_EQ(d.get(0, 0), (Rgb{100, 50, 0}));
    EXPECT_EQ(d.get(1, 0), (Rgb{0, 0, 0}));
}

TEST(Viz, CameraFitsTheBoxAndProjectsTopView) {
    const Camera cam = Camera::fit(topView(), {0.0, 0.0, 0.0}, {1.0, 1.0, 0.0});
    const Vec3 a = cam.project({0.0, 0.0, 0.0});
    const Vec3 b = cam.project({1.0, 1.0, 0.0});
    EXPECT_NEAR(a[0], 0.0, 1e-9);   // x = 0 at the left edge
    EXPECT_NEAR(b[0], 200.0, 1e-9); // x = 1 at the right edge
    EXPECT_NEAR(a[1], 200.0, 1e-9); // y = 0 at the bottom (screen y grows down)
    EXPECT_NEAR(b[1], 0.0, 1e-9);
    EXPECT_GT(cam.project({0.5, 0.5, 1.0})[2], cam.project({0.5, 0.5, 0.0})[2]); // nearer
}

TEST(Viz, DepthBufferKeepsTheNearerSurface) {
    const Rgb red{220, 30, 30}, blue{30, 30, 220};
    const TriangleMesh far = square(0.0, 1.0, 0.0, 1.0, 0.0, blue);
    const TriangleMesh nearHalf = square(0.0, 0.5, 0.0, 1.0, 0.5, red); // left half, above
    const Camera cam = Camera::fit(topView(), {0.0, 0.0, 0.0}, {1.0, 1.0, 0.5});
    // Draw order must not matter.
    for (const auto& order : {std::vector<const TriangleMesh*>{&far, &nearHalf},
                              std::vector<const TriangleMesh*>{&nearHalf, &far}}) {
        const Image img = render(cam, order, {});
        EXPECT_EQ(img.get(50, 100), red);
        EXPECT_EQ(img.get(150, 100), blue);
    }
}

TEST(Viz, SegmentsOnASurfaceShowAndHiddenOnesDoNot) {
    const TriangleMesh top = square(0.0, 1.0, 0.0, 1.0, 1.0, {200, 200, 200});
    const Rgb black{0, 0, 0};
    std::vector<Segment> segs = {{{0.5, 0.0, 1.0}, {0.5, 1.0, 1.0}, black},    // on the surface
                                 {{0.0, 0.25, 0.0}, {1.0, 0.25, 0.0}, black}}; // below it
    ViewSpec v = topView();
    v.lineWidth = 2.0;
    const Camera cam = Camera::fit(v, {0.0, 0.0, 0.0}, {1.0, 1.0, 1.0});
    const Image img = render(cam, {&top}, segs);
    EXPECT_EQ(img.get(100, 100), black);               // visible edge
    EXPECT_EQ(img.get(40, 150), (Rgb{200, 200, 200})); // hidden edge stays covered
}

TEST(Viz, ShadingDependsOnTheNormalOnly) {
    TriangleMesh m = square(0.0, 1.0, 0.0, 1.0, 0.0, {200, 200, 200});
    m.shaded = true;
    const Camera cam = Camera::fit(topView(), {0.0, 0.0, 0.0}, {1.0, 1.0, 0.0});
    const Image img = render(cam, {&m}, {});
    EXPECT_EQ(img.get(20, 20), img.get(180, 180)); // flat face: uniform shade
    EXPECT_LT(img.get(100, 100)[0], 200);          // and darker than full light
}

TEST(Viz, SpyMarksTheDiagonalAndMirrorsAFactor) {
    // 100 × 100 diagonal plus the first column.
    const std::int64_t n = 100;
    std::vector<std::int64_t> ptr{0};
    std::vector<std::int32_t> idx;
    for (std::int32_t i = 0; i < n; ++i) {
        if (i > 0) {
            idx.push_back(0);
        }
        idx.push_back(i);
        ptr.push_back(static_cast<std::int64_t>(idx.size()));
    }
    SpyOptions o;
    o.pixels = 50;
    const Image img = renderSpy(n, ptr, idx, false, o);
    const Rgb white{255, 255, 255};
    EXPECT_NE(img.get(10, 10), white); // diagonal
    EXPECT_NE(img.get(0, 30), white);  // column 0 of row 60
    EXPECT_EQ(img.get(30, 0), white);  // nothing in the upper triangle
    o.mirror = true;
    const Image both = renderSpy(n, ptr, idx, false, o);
    EXPECT_NE(both.get(30, 0), white);
}

TEST(Viz, Base64MatchesRfc4648Vectors) {
    const auto b = [](const std::string& s) {
        return base64(std::vector<std::uint8_t>(s.begin(), s.end()));
    };
    EXPECT_EQ(b(""), "");
    EXPECT_EQ(b("f"), "Zg==");
    EXPECT_EQ(b("fo"), "Zm8=");
    EXPECT_EQ(b("foo"), "Zm9v");
    EXPECT_EQ(b("foobar"), "Zm9vYmFy");
}

TEST(Viz, CanvasEscapesTextAndEmbedsImages) {
    SvgCanvas c(300, 200, "a < b");
    c.text(10, 20, "T & q");
    c.image({1, 2, 3}, 0, 0, 10, 10);
    c.colorbar(250, 20, 12, 150, 25.0, 99.1, "°C");
    const std::string svg = c.str();
    EXPECT_NE(svg.find("<title>a &lt; b</title>"), std::string::npos);
    EXPECT_NE(svg.find("T &amp; q"), std::string::npos);
    EXPECT_NE(svg.find("data:image/png;base64,AQID"), std::string::npos);
    EXPECT_NE(svg.find(">99.1<"), std::string::npos);
    EXPECT_EQ(c.str(), svg); // deterministic
}

TEST(Viz, LabelNumberKeepsSignificantDigits) {
    EXPECT_EQ(labelNumber(99.0916, 4), "99.09");
    EXPECT_EQ(labelNumber(3759720.0, 3), "3.76e+06");
    EXPECT_EQ(labelNumber(0.5, 3), "0.5");
}

TEST(SvgPlotExtras, HollowAndCrossMarkersAndAnnotations) {
    PlotSpec spec;
    spec.title = "t";
    spec.logX = spec.logY = true;
    PlotSeries s;
    s.x = {1e4, 1e5};
    s.y = {1.0, 10.0};
    s.marker = PlotMarker::Triangle;
    s.hollow = true;
    PlotSeries f;
    f.x = {1e6};
    f.y = {5.0};
    f.line = false;
    f.marker = PlotMarker::Cross;
    spec.series = {s, f};
    spec.annotations = {{1e6, 5.0, "over 8 GB"}};
    spec.note = "data: E2.csv";
    const auto svg = renderSvgPlot(spec);
    ASSERT_TRUE(svg);
    EXPECT_NE(svg.value().find("fill=\"#FFFFFF\" stroke="), std::string::npos);
    EXPECT_NE(svg.value().find("<path d=\"M"), std::string::npos);
    EXPECT_NE(svg.value().find("over 8 GB"), std::string::npos);
    EXPECT_NE(svg.value().find("data: E2.csv"), std::string::npos);
}

TEST(SvgPlotExtras, StackedBarsValidateAndLabelTotals) {
    BarChartSpec spec;
    spec.title = "setup + solve";
    spec.yLabel = "time (s)";
    spec.categories = {"ND", "PCG-J\n1 load case"};
    spec.stacks = {{"setup", "#0072B2", {3.7, 0.0}, true}, {"solve", "#0072B2", {0.02, 0.51}}};
    const auto svg = renderSvgBars(spec);
    ASSERT_TRUE(svg);
    EXPECT_NE(svg.value().find(">3.72<"), std::string::npos);
    EXPECT_NE(svg.value().find("1 load case"), std::string::npos);
    spec.stacks[0].values = {-1.0, 0.0};
    EXPECT_FALSE(renderSvgBars(spec));
}

} // namespace
} // namespace uniforge::viz
