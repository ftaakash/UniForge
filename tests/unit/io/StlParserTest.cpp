// ASCII STL parser, positive cases: counts, bbox, normals, degenerate triangles, encodings,
// limits, progress and cancellation. Malformed files are covered by tests/negative/io.

#include "core/io/StlParser.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "core/common/CancelToken.hpp"
#include "core/io/StlWriter.hpp"

using uniforge::CancelToken;
using uniforge::ErrorCode;
using uniforge::Result;
using namespace uniforge::io;

namespace {

Result<StlMesh> parse(const std::string& text, const ImportLimits& limits = {}) {
    std::istringstream in(text);
    return parseAsciiStl(in, text.size(), limits);
}

std::string oneFacet(const std::string& normal, const std::string& v0, const std::string& v1,
                     const std::string& v2) {
    return "solid t\n facet normal " + normal + "\n  outer loop\n   vertex " + v0 + "\n   vertex " +
           v1 + "\n   vertex " + v2 + "\n  endloop\n endfacet\nendsolid t\n";
}

// n facets in a row along x, each a right triangle of unit legs.
std::string strip(std::size_t n) {
    std::string out;
    appendStlBegin(out, "strip");
    for (std::size_t i = 0; i < n; ++i) {
        const double x = static_cast<double>(i);
        appendStlFacet(out, {Vec3d{x, 0, 0}, Vec3d{x + 1, 0, 0}, Vec3d{x, 1, 0}});
    }
    appendStlEnd(out, "strip");
    return out;
}

void expectVec(const Vec3d& v, double x, double y, double z) {
    EXPECT_DOUBLE_EQ(v.x, x);
    EXPECT_DOUBLE_EQ(v.y, y);
    EXPECT_DOUBLE_EQ(v.z, z);
}

} // namespace

TEST(StlParser, SingleFacetCountsBboxAndName) {
    const auto r = parse(oneFacet("0 0 1", "0 0 0", "2 0 0", "0 3 -1"));
    ASSERT_TRUE(r.ok()) << r.error().message;
    const StlMesh& m = r.value();
    EXPECT_EQ(m.name, "t");
    ASSERT_EQ(m.tri.size(), 1U);
    ASSERT_EQ(m.normal.size(), 1U);
    expectVec(m.tri[0][2], 0, 3, -1);
    expectVec(m.bbMin, 0, 0, -1);
    expectVec(m.bbMax, 2, 3, 0);
    EXPECT_EQ(m.degenerate, 0U);
    EXPECT_EQ(m.recomputedNormals, 0U);
    EXPECT_TRUE(m.stats.warnings.empty());
    EXPECT_EQ(m.stats.lines, 9U);
    EXPECT_GT(m.stats.bytes, 0U);
    EXPECT_GE(m.stats.parseSeconds, 0.0);
}

TEST(StlParser, ManyFacetsFromWriterRoundTrip) {
    const auto r = parse(strip(1000));
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().name, "strip");
    EXPECT_EQ(r.value().tri.size(), 1000U);
    expectVec(r.value().bbMin, 0, 0, 0);
    expectVec(r.value().bbMax, 1000, 1, 0);
    for (const Vec3d& n : r.value().normal) {
        expectVec(n, 0, 0, 1);
    }
}

TEST(StlParser, ZeroNormalIsRecomputedFromVertexOrder) {
    // Counter-clockwise seen from +z -> +z; clockwise -> -z.
    auto r = parse(oneFacet("0 0 0", "0 0 0", "1 0 0", "0 1 0"));
    ASSERT_TRUE(r.ok());
    expectVec(r.value().normal[0], 0, 0, 1);
    EXPECT_EQ(r.value().recomputedNormals, 1U);
    ASSERT_EQ(r.value().stats.warnings.size(), 1U);
    EXPECT_NE(r.value().stats.warnings[0].find("recomputed"), std::string::npos);

    r = parse(oneFacet("0 0 0", "0 0 0", "0 1 0", "1 0 0"));
    ASSERT_TRUE(r.ok());
    expectVec(r.value().normal[0], 0, 0, -1);
}

TEST(StlParser, FileNormalIsNormalisedNotReplaced) {
    // The file normal wins over the vertex order (exporters may use either convention).
    const auto r = parse(oneFacet("0 0 -5", "0 0 0", "1 0 0", "0 1 0"));
    ASSERT_TRUE(r.ok());
    expectVec(r.value().normal[0], 0, 0, -1);
    EXPECT_EQ(r.value().recomputedNormals, 0U);
}

TEST(StlParser, TinyAndHugeCoordinatesGiveUnitNormals) {
    for (const char* scale : {"1e-200", "1e200"}) {
        const std::string s(scale);
        const auto r = parse(oneFacet("0 0 0", "0 0 0", s + " 0 0", "0 " + s + " 0"));
        ASSERT_TRUE(r.ok()) << scale;
        expectVec(r.value().normal[0], 0, 0, 1);
        EXPECT_EQ(r.value().degenerate, 0U) << scale;
    }
}

TEST(StlParser, DegenerateTrianglesAreCountedAndKept) {
    std::string text = "solid d\n";
    const auto facet = [&](const char* a, const char* b, const char* c) {
        text += std::string("facet normal 0 0 0\nouter loop\nvertex ") + a + "\nvertex " + b +
                "\nvertex " + c + "\nendloop\nendfacet\n";
    };
    facet("0 0 0", "1 0 0", "0 1 0"); // fine
    facet("0 0 0", "1 1 1", "2 2 2"); // collinear
    facet("1 2 3", "1 2 3", "1 2 3"); // a point
    facet("0 0 0", "1 0 0", "1 0 0"); // repeated vertex
    text += "endsolid d\n";
    const auto r = parse(text);
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().tri.size(), 4U);
    EXPECT_EQ(r.value().degenerate, 3U);
    EXPECT_EQ(r.value().recomputedNormals, 1U);
    expectVec(r.value().normal[2], 0, 0, 0);
    bool warned = false;
    for (const auto& w : r.value().stats.warnings) {
        warned = warned || w.rfind("3 degenerate", 0) == 0;
    }
    EXPECT_TRUE(warned);
}

TEST(StlParser, BomCrlfUppercaseAndPlusSignsFromFile) {
    const auto r = parseAsciiStlFile(std::filesystem::path(UNIFORGE_FIXTURES_DIR) / "stl" /
                                     "valid_bom_crlf.stl");
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().name, "crlf");
    ASSERT_EQ(r.value().tri.size(), 1U);
    expectVec(r.value().tri[0][1], 1, 0, 0);
    expectVec(r.value().tri[0][2], 0, 1, 0);
    expectVec(r.value().normal[0], 0, 0, 1);
}

TEST(StlParser, BlankLinesTabsAndMultipleSolids) {
    const std::string text = "\n\nsolid a\n\n\tfacet\tnormal 0 0 1\n outer loop\n vertex 0 0 0\n"
                             " vertex 1 0 0\n vertex 0 1 0\n endloop\n endfacet\nendsolid a\n\n"
                             "solid b\nfacet normal 0 0 1\nouter loop\nvertex 0 0 5\nvertex 1 0 5\n"
                             "vertex 0 1 5\nendloop\nendfacet\nendsolid\n\n";
    const auto r = parse(text);
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().name, "a");
    EXPECT_EQ(r.value().tri.size(), 2U);
    EXPECT_DOUBLE_EQ(r.value().bbMax.z, 5.0);
    ASSERT_EQ(r.value().stats.warnings.size(), 1U);
    EXPECT_NE(r.value().stats.warnings[0].find("2 solids"), std::string::npos);
}

TEST(StlParser, MissingEndsolidIsAWarningNotAnError) {
    std::string text = strip(2);
    text.erase(text.rfind("endsolid"));
    const auto r = parse(text);
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().tri.size(), 2U);
    ASSERT_EQ(r.value().stats.warnings.size(), 1U);
    EXPECT_NE(r.value().stats.warnings[0].find("endsolid"), std::string::npos);
}

TEST(StlParser, UnnamedSolidTakesTheFileStem) {
    const auto dir = std::filesystem::temp_directory_path() / "uniforge_stl_name_test";
    std::filesystem::create_directories(dir);
    const auto file = dir / "bracket v2.stl";
    {
        std::ofstream out(file, std::ios::binary);
        out << "solid\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\n"
               "endloop\nendfacet\nendsolid\n";
    }
    const auto r = parseAsciiStlFile(file);
    std::filesystem::remove_all(dir);
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().name, "bracket v2");
}

TEST(StlParser, FacetLimitExceeded) {
    ImportLimits limits;
    limits.maxFacets = 3;
    EXPECT_TRUE(parse(strip(3), limits).ok());
    const auto r = parse(strip(4), limits);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::LimitExceeded);
    EXPECT_EQ(r.error().line, 1U + 3U * 7U + 1U); // the 4th `facet` line
    EXPECT_NE(r.error().hint.find("3 facets"), std::string::npos) << r.error().hint;
}

TEST(StlParser, ByteLimitExceededUpFrontAndWhileStreaming) {
    const std::string text = strip(100);
    ImportLimits limits;
    limits.maxBytes = 1000;
    auto r = parse(text, limits); // sizeHint > limit: rejected before reading
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::LimitExceeded);

    std::istringstream in(text);
    r = parseAsciiStl(in, 0, limits); // size unknown: caught by the reader
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::LimitExceeded);
}

TEST(StlParser, BinarySizeRuleWithSolidHeaderAndNoNulBytes) {
    // An 84-byte "solid ..." header whose bytes 80..83 read as count n, and a stated size of
    // 84 + 50 n: binary STL even though the text has no NUL byte.
    std::string head = "solid but really binary";
    head.resize(80, ' ');
    head += "AAAA"; // n = 0x41414141
    const std::uintmax_t n = 0x41414141ULL;
    std::istringstream in(head);
    const auto r = parseAsciiStl(in, 84 + 50 * n, ImportLimits{~std::uintmax_t{0}});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::UnsupportedFormat);
    EXPECT_NE(r.error().hint.find("Export as ASCII STL"), std::string::npos);
}

TEST(StlParser, ProgressReachesOne) {
    const std::string text = strip(5000); // several 64 KB batches
    std::istringstream in(text);
    double last = -1.0;
    int calls = 0;
    const auto r = parseAsciiStl(in, text.size(), {}, [&](double f, std::string_view) {
        EXPECT_GE(f, last);
        last = f;
        ++calls;
    });
    ASSERT_TRUE(r.ok());
    EXPECT_GT(calls, 2);
    EXPECT_DOUBLE_EQ(last, 1.0);
}

TEST(StlParser, CancelReturnsCancelled) {
    const std::string text = strip(5000);
    CancelToken cancel;
    cancel.requestCancel();
    std::istringstream in(text);
    auto r = parseAsciiStl(in, text.size(), {}, {}, &cancel);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::Cancelled);

    // Cancelled from the progress callback in the middle of the file.
    cancel.reset();
    std::istringstream in2(text);
    r = parseAsciiStl(
        in2, text.size(), {},
        [&](double f, std::string_view) {
            if (f > 0.3) {
                cancel.requestCancel();
            }
        },
        &cancel);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::Cancelled);
}
