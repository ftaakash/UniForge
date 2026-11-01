#include "core/io/StlParser.hpp"

#include <chrono>
#include <cmath>
#include <fstream>
#include <string_view>
#include <utility>

#include "core/io/ImportErrors.hpp"
#include "core/io/LineReader.hpp"
#include "core/io/TextParse.hpp"

namespace uniforge::io {

namespace {

using detail::lineError;

constexpr std::string_view kKind = "STL file";
constexpr std::string_view kReExport =
    "The file may be damaged or hand-edited; re-export it as ASCII STL.";
constexpr std::string_view kNumberHint =
    "Coordinates must be plain decimal numbers with '.' as the decimal separator, e.g. 0.5 or "
    "-1.25e-3.";

// Whitespace-separated tokens of one line. Only the first kMax are kept; `count` is the total.
struct Tokens {
    static constexpr std::size_t kMax = 6;
    std::string_view t[kMax];
    std::size_t count = 0;
};

Tokens tokenize(std::string_view line) {
    Tokens tk;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            ++i;
        }
        if (i == line.size()) {
            break;
        }
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') {
            ++i;
        }
        if (tk.count < Tokens::kMax) {
            tk.t[tk.count] = line.substr(start, i - start);
        }
        ++tk.count;
    }
    return tk;
}

// ASCII case-insensitive keyword match ("FACET" == "facet").
bool is(std::string_view token, std::string_view keyword) noexcept {
    if (token.size() != keyword.size()) {
        return false;
    }
    for (std::size_t i = 0; i < token.size(); ++i) {
        char c = token[i];
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
        if (c != keyword[i]) {
            return false;
        }
    }
    return true;
}

bool isKeyword(std::string_view token) noexcept {
    for (const std::string_view kw :
         {"solid", "endsolid", "facet", "outer", "vertex", "endloop", "endfacet"}) {
        if (is(token, kw)) {
            return true;
        }
    }
    return false;
}

// "Line N: expected `X` but found `line`." with a hint for unknown vs misplaced keywords.
Error unexpected(std::size_t line, std::string_view expected, std::string_view text,
                 std::string_view keyword) {
    std::string hint = isKeyword(keyword)
                           ? "The keywords are out of order. " + std::string(kReExport)
                           : "`" + quoteForMessage(keyword, 30) +
                                 "` is not an ASCII STL keyword. " + std::string(kReExport);
    return lineError(ErrorCode::ParseError, line,
                     "expected " + std::string(expected) + " but found `" +
                         quoteForMessage(trimBlanks(text)) + "`.",
                     std::move(hint));
}

// Parses tokens t[first..first+2] into v; returns false and sets `error` on failure.
bool parseVec(const Tokens& tk, std::size_t first, std::size_t line, std::string_view field,
              Vec3d& v, Error& error) {
    double* out[3] = {&v.x, &v.y, &v.z};
    for (std::size_t i = 0; i < 3; ++i) {
        const std::string_view token = tk.t[first + i];
        const NumberStatus s = parseDouble(token, *out[i]);
        if (s != NumberStatus::Ok) {
            error = detail::numberError(s, line, field, token, kNumberHint);
            return false;
        }
    }
    return true;
}

Vec3d sub(const Vec3d& a, const Vec3d& b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

double maxAbs(const Vec3d& v) noexcept {
    return std::fmax(std::fabs(v.x), std::fmax(std::fabs(v.y), std::fabs(v.z)));
}

// Unit vector of v, or nullopt-like (false) when v is zero or its length is not finite.
bool normalise(Vec3d v, Vec3d& out) noexcept {
    const double s = maxAbs(v); // scale first so huge or tiny components cannot overflow
    if (!(s > 0.0) || !std::isfinite(s)) {
        return false;
    }
    v = {v.x / s, v.y / s, v.z / s};
    const double len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    out = {v.x / len, v.y / len, v.z / len};
    return true;
}

// Relative zero-area test: |e1 x e2| tiny compared with the squared longest edge.
constexpr double kDegenerateRelTol = 1e-12;

struct FacetResult {
    Vec3d normal{};
    bool degenerate = false;
    bool recomputed = false;
};

FacetResult finishFacet(const std::array<Vec3d, 3>& v, const Vec3d& fileNormal) noexcept {
    FacetResult r;
    Vec3d e1 = sub(v[1], v[0]);
    Vec3d e2 = sub(v[2], v[0]);
    const Vec3d e3 = sub(v[2], v[1]);
    const double s = std::fmax(maxAbs(e1), std::fmax(maxAbs(e2), maxAbs(e3)));
    Vec3d cross{};
    if (s > 0.0 && std::isfinite(s)) {
        e1 = {e1.x / s, e1.y / s, e1.z / s};
        e2 = {e2.x / s, e2.y / s, e2.z / s};
        cross = {e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        // Longest edge has length >= 1 after scaling, so |cross| compares with 1.
        r.degenerate = std::sqrt(cross.x * cross.x + cross.y * cross.y + cross.z * cross.z) <=
                       kDegenerateRelTol;
    } else {
        r.degenerate = true;
    }

    const bool fileNormalZero = fileNormal.x == 0.0 && fileNormal.y == 0.0 && fileNormal.z == 0.0;
    if (!fileNormalZero && normalise(fileNormal, r.normal)) {
        return r;
    }
    r.normal = {};
    if (!r.degenerate) {
        r.recomputed = normalise(cross, r.normal);
    }
    return r;
}

enum class State { Start, Solid, Facet, Loop, LoopDone, AfterSolid };

} // namespace

Result<StlMesh> parseAsciiStl(std::istream& in, std::uintmax_t sizeHint, const ImportLimits& limits,
                              ProgressFn progress, const CancelToken* cancel) {
    const auto started = std::chrono::steady_clock::now();
    if (sizeHint > limits.maxBytes) {
        return detail::fileTooLargeError(sizeHint, limits, kKind);
    }
    LineReader reader(in, sizeHint, limits.maxBytes, limits.maxLineBytes, std::move(progress),
                      cancel, "Reading STL");

    // Binary STL: 80-byte header, uint32 triangle count, 50 bytes per triangle. Some exporters
    // start the header with "solid", so the size rule and NUL bytes are checked first.
    const std::string_view head = reader.peek(84);
    const bool hasNul = head.find('\0') != std::string_view::npos;
    std::uint32_t declared = 0;
    if (head.size() >= 84) {
        for (int i = 3; i >= 0; --i) {
            declared = (declared << 8) |
                       static_cast<unsigned char>(head[80 + static_cast<std::size_t>(i)]);
        }
    }
    const bool binarySize = head.size() >= 84 && sizeHint == 84 + 50ULL * declared;
    if (hasNul || binarySize) {
        return makeError(ErrorCode::UnsupportedFormat,
                         binarySize ? "This is a binary STL file (its header declares " +
                                          countOf(declared, "triangle", "triangles") +
                                          "); UniForge reads ASCII STL only."
                                    : "This file contains binary data; it is not an ASCII STL "
                                      "file.",
                         "Export as ASCII STL (in most CAD tools: Save As, STL, ASCII), then "
                         "import it again.");
    }

    StlMesh mesh;
    BoundsAccumulator bounds;
    State state = State::Start;
    std::size_t solids = 0;
    std::size_t solidLine = 0;
    std::size_t facetLine = 0;
    std::size_t vertices = 0;
    std::array<Vec3d, 3> v{};
    Vec3d fileNormal{};
    Error error;

    std::string_view line;
    LineStatus status = LineStatus::Line;
    while ((status = reader.next(line)) == LineStatus::Line) {
        const Tokens tk = tokenize(line);
        if (tk.count == 0) {
            continue;
        }
        const std::size_t ln = reader.lineNumber();
        const std::string_view kw = tk.t[0];
        switch (state) {
        case State::Start:
            if (!is(kw, "solid")) {
                return lineError(ErrorCode::UnsupportedFormat, ln,
                                 "an ASCII STL file must start with `solid`, but this one starts "
                                 "with `" +
                                     quoteForMessage(trimBlanks(line)) + "`.",
                                 "This is not an ASCII STL file; it may be binary STL or another "
                                 "format. Export as ASCII STL.");
            }
            mesh.name = quoteForMessage(trimBlanks(trimBlanks(line).substr(kw.size())), 80);
            solids = 1;
            solidLine = ln;
            state = State::Solid;
            break;
        case State::Solid:
            if (is(kw, "facet") && tk.count == 5 && is(tk.t[1], "normal")) {
                if (mesh.tri.size() == limits.maxFacets) {
                    return lineError(ErrorCode::LimitExceeded, ln,
                                     "the model has more than " + formatCount(limits.maxFacets) +
                                         " facets.",
                                     "UniForge is tested up to " + formatCount(limits.maxFacets) +
                                         " facets (results/benchmarks/import_limits.md). "
                                         "Simplify (decimate) or split the model.");
                }
                if (!parseVec(tk, 2, ln, "normal component", fileNormal, error)) {
                    return error;
                }
                facetLine = ln;
                state = State::Facet;
            } else if (is(kw, "endsolid")) {
                state = State::AfterSolid;
            } else {
                return unexpected(ln, "`facet normal nx ny nz` or `endsolid`", line, kw);
            }
            break;
        case State::Facet:
            if (is(kw, "outer") && tk.count == 2 && is(tk.t[1], "loop")) {
                vertices = 0;
                state = State::Loop;
            } else {
                return unexpected(ln, "`outer loop`", line, kw);
            }
            break;
        case State::Loop:
            if (is(kw, "vertex")) {
                if (vertices == 3) {
                    return lineError(ErrorCode::ParseError, ln,
                                     "the facet that starts on line " + std::to_string(facetLine) +
                                         " has a 4th vertex; ASCII STL facets are triangles.",
                                     "Each `outer loop` must hold exactly 3 `vertex` lines. " +
                                         std::string(kReExport));
                }
                if (tk.count != 4) {
                    return lineError(ErrorCode::ParseError, ln,
                                     "`vertex` needs 3 coordinates (x y z) but has " +
                                         std::to_string(tk.count - 1) + ".",
                                     std::string(kReExport));
                }
                if (!parseVec(tk, 1, ln, "vertex coordinate", v[vertices], error)) {
                    return error;
                }
                ++vertices;
            } else if (is(kw, "endloop") && tk.count == 1) {
                if (vertices != 3) {
                    return lineError(ErrorCode::ParseError, ln,
                                     "the facet that starts on line " + std::to_string(facetLine) +
                                         " has " + std::to_string(vertices) +
                                         (vertices == 1 ? " vertex" : " vertices") +
                                         "; a triangle needs exactly 3.",
                                     "Each `outer loop` must hold exactly 3 `vertex` lines. " +
                                         std::string(kReExport));
                }
                state = State::LoopDone;
            } else {
                return unexpected(ln, vertices < 3 ? "`vertex x y z`" : "`endloop`", line, kw);
            }
            break;
        case State::LoopDone:
            if (is(kw, "endfacet") && tk.count == 1) {
                const FacetResult r = finishFacet(v, fileNormal);
                mesh.tri.push_back(v);
                mesh.normal.push_back(r.normal);
                mesh.degenerate += r.degenerate ? 1U : 0U;
                mesh.recomputedNormals += r.recomputed ? 1U : 0U;
                for (const Vec3d& p : v) {
                    bounds.add(p);
                }
                state = State::Solid;
            } else {
                return unexpected(ln, "`endfacet`", line, kw);
            }
            break;
        case State::AfterSolid:
            if (is(kw, "solid")) {
                ++solids;
                state = State::Solid;
            } else {
                return unexpected(ln, "the end of the file or another `solid`", line, kw);
            }
            break;
        }
    }
    if (status != LineStatus::End) {
        return detail::readerError(status, reader, limits, kKind);
    }

    switch (state) {
    case State::Start:
        return makeError(ErrorCode::EmptyFile,
                         reader.bytesRead() == 0 ? "The STL file is empty (0 bytes)."
                                                 : "The STL file contains only blank lines.",
                         "Choose an ASCII STL file that contains a model, or export the model "
                         "again.");
    case State::Facet:
    case State::Loop:
    case State::LoopDone:
        return lineError(ErrorCode::ParseError, reader.lineNumber(),
                         "the file ends inside the facet that starts on line " +
                             std::to_string(facetLine) + ".",
                         "The file is truncated (incomplete export, copy or download). Export or "
                         "copy it again.");
    case State::Solid:
        mesh.stats.warnings.push_back(
            "The file has no closing `endsolid` line; it may be truncated after the last "
            "complete facet.");
        break;
    case State::AfterSolid:
        break;
    }
    if (mesh.tri.empty()) {
        return lineError(ErrorCode::EmptyFile, solidLine,
                         "the STL solid" + (mesh.name.empty() ? "" : " `" + mesh.name + "`") +
                             " contains no facets.",
                         "The exporter wrote an empty solid. Check that the part has geometry "
                         "and export it again.");
    }

    if (solids > 1) {
        mesh.stats.warnings.push_back("The file contains " + std::to_string(solids) +
                                      " solids; they were merged into one mesh.");
    }
    if (mesh.recomputedNormals > 0) {
        mesh.stats.warnings.push_back(
            countOf(mesh.recomputedNormals, "facet normal was", "facet normals were") +
            " zero and recomputed from the vertex order (right-hand rule).");
    }
    if (mesh.degenerate > 0) {
        mesh.stats.warnings.push_back(countOf(mesh.degenerate, "degenerate (zero-area) triangle",
                                              "degenerate (zero-area) triangles") +
                                      "; kept, but without a defined normal.");
    }
    mesh.bbMin = bounds.min();
    mesh.bbMax = bounds.max();
    mesh.stats.bytes = reader.bytesRead();
    mesh.stats.lines = reader.lineNumber();
    mesh.stats.parseSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return mesh;
}

Result<StlMesh> parseAsciiStlFile(const std::filesystem::path& path, const ImportLimits& limits,
                                  ProgressFn progress, const CancelToken* cancel) {
    std::ifstream stream;
    auto size = detail::openForImport(path, stream);
    if (!size) {
        return std::move(size).error();
    }
    auto mesh = parseAsciiStl(stream, size.value(), limits, std::move(progress), cancel);
    if (mesh && mesh.value().name.empty()) {
        mesh.value().name = quoteForMessage(path.stem().u8string(), 80);
    }
    return mesh;
}

} // namespace uniforge::io
