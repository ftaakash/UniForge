#include "core/study/Figures.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>

#include "core/common/AtomicFile.hpp"
#include "core/common/SvgPlot.hpp"
#include "core/fem3d/Assembly.hpp"
#include "core/fem3d/Design.hpp"
#include "core/fem3d/HexMesh.hpp"
#include "core/fem3d/Post.hpp"
#include "core/solvers/LinearSolver.hpp"
#include "core/solvers/amg/Amg.hpp"
#include "core/solvers/direct/Cholesky.hpp"
#include "core/study/Plan.hpp"
#include "core/viz/Colormap.hpp"
#include "core/viz/PngWriter.hpp"
#include "core/viz/Raster3D.hpp"
#include "core/viz/Spy.hpp"
#include "core/viz/SvgCanvas.hpp"

namespace uniforge::study {

namespace {

using viz::Rgb;
using viz::SvgCanvas;
using viz::Vec3;
using Row = std::map<std::string, std::string>;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr const char* kInk = "#1A1E24";
constexpr const char* kMuted = "#59616E";
constexpr const char* kGrey = "#7A828E";

// ---- Results tables ---------------------------------------------------------------------------

std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : line) {
        if (c == ',') {
            out.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

// Result CSVs are written comma-free and unquoted (RunRecord::toCsv), so a split is enough.
Result<std::vector<Row>> readCsv(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return makeError(ErrorCode::IoError, "Cannot read " + file.u8string(),
                         "Run `uniforge_cli study collect` first.");
    }
    std::string line;
    std::getline(in, line);
    const auto header = splitCsvLine(line);
    std::vector<Row> rows;
    while (std::getline(in, line)) {
        if (line.empty() || line == "\r") {
            continue;
        }
        const auto cells = splitCsvLine(line);
        Row r;
        for (std::size_t i = 0; i < header.size() && i < cells.size(); ++i) {
            r[header[i]] = cells[i];
        }
        rows.push_back(std::move(r));
    }
    return rows;
}

double num(const Row& r, const std::string& col) {
    const auto it = r.find(col);
    if (it == r.end() || it->second.empty()) {
        return kNaN;
    }
    char* end = nullptr;
    const double v = std::strtod(it->second.c_str(), &end);
    return end != it->second.c_str() ? v : kNaN;
}

std::string str(const Row& r, const std::string& col) {
    const auto it = r.find(col);
    return it == r.end() ? std::string() : it->second;
}

struct Results {
    std::vector<Row> runs;
    std::vector<Row> e5;
    std::filesystem::path root;

    [[nodiscard]] const Row* find(const std::string& id) const {
        for (const Row& r : runs) {
            if (str(r, "id") == id) {
                return &r;
            }
        }
        return nullptr;
    }
    [[nodiscard]] std::vector<const Row*> where(const std::function<bool(const Row&)>& f) const {
        std::vector<const Row*> out;
        for (const Row& r : runs) {
            if (f(r)) {
                out.push_back(&r);
            }
        }
        return out;
    }
};

bool converged(const Row& r) {
    return str(r, "solve_status") == "Converged";
}
double totalSeconds(const Row& r) {
    return num(r, "setup_s") + num(r, "solve_s_median");
}

std::vector<std::pair<double, double>> history(const Results& res, const std::string& id,
                                               const std::string& xCol, const std::string& yCol) {
    std::vector<std::pair<double, double>> out;
    auto rows = readCsv(res.root / "history" / (id + ".csv"));
    if (!rows) {
        return out;
    }
    for (const Row& r : rows.value()) {
        const double x = num(r, xCol), y = num(r, yCol);
        if (std::isfinite(x) && std::isfinite(y) && y > 0.0) {
            out.emplace_back(x, y);
        }
    }
    return out;
}

// Least-squares slope of log y over log x.
double logSlope(const std::vector<double>& x, const std::vector<double>& y) {
    if (x.size() < 2) {
        return kNaN;
    }
    double mx = 0.0, my = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        mx += std::log(x[i]);
        my += std::log(y[i]);
    }
    mx /= static_cast<double>(x.size());
    my /= static_cast<double>(x.size());
    double sxy = 0.0, sxx = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        sxy += (std::log(x[i]) - mx) * (std::log(y[i]) - my);
        sxx += (std::log(x[i]) - mx) * (std::log(x[i]) - mx);
    }
    return sxx > 0.0 ? sxy / sxx : kNaN;
}

std::string fmt(double v, const char* f = "%.3g") {
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, f, v);
    return buffer;
}

// ---- Figure tokens (docs/report_v2/figure_tokens.md) -------------------------------------------

constexpr const char* kDirect = "#0072B2";
constexpr const char* kPcg = "#E69F00";
constexpr const char* kAmg = "#009E73";

const std::vector<std::string>& solverOrder() {
    static const std::vector<std::string> order = {"DIRECT-ND", "DIRECT-RCM", "PCG-J",
                                                   "PCG-IC0",   "AMG",        "AMG-PCG"};
    return order;
}

std::string familyColor(const std::string& solver) {
    if (solver.rfind("DIRECT", 0) == 0) {
        return kDirect;
    }
    if (solver.rfind("PCG", 0) == 0) {
        return kPcg;
    }
    return kAmg;
}

PlotSeries styled(const std::string& solver) {
    PlotSeries s;
    s.label = solver;
    s.color = familyColor(solver);
    if (solver == "DIRECT-ND" || solver == "PCG-J") {
        s.marker = PlotMarker::Circle;
    } else if (solver == "DIRECT-RCM") {
        s.marker = PlotMarker::Circle;
        s.hollow = true;
        s.dashed = true;
    } else if (solver == "PCG-IC0") {
        s.marker = PlotMarker::Square;
        s.dashed = true;
    } else if (solver == "AMG") {
        s.marker = PlotMarker::Triangle;
    } else if (solver == "AMG-PCG") {
        s.marker = PlotMarker::Triangle;
        s.hollow = true;
        s.dashed = true;
    } else {
        s.marker = PlotMarker::Diamond;
        s.dashed = true;
    }
    return s;
}

PlotSeries reference(const std::string& label, std::vector<double> x, std::vector<double> y) {
    PlotSeries s;
    s.label = label;
    s.x = std::move(x);
    s.y = std::move(y);
    s.color = kGrey;
    s.dashed = true;
    s.strokeWidth = 1.0;
    return s;
}

std::string shortReason(const Row& r) {
    const std::string setup = str(r, "setup_status");
    const std::string solve = str(r, "solve_status");
    if (setup == "OverBudget") {
        return "over 8 GB";
    }
    if (setup == "Breakdown" || solve == "Breakdown") {
        return "breakdown";
    }
    if (solve == "Stagnated") {
        return "stagnated";
    }
    if (solve == "MaxIterations") {
        return "iteration cap";
    }
    if (solve == "TimeCap") {
        return "time cap";
    }
    return solve;
}

// ---- Geometry, fields and renders -------------------------------------------------------------

struct Solved {
    fem3d::Design design;
    fem3d::HexMesh mesh;
    fem3d::LinearSystem sys;
    solvers::Vector T;
};

Result<Solved> meshAndSolve(fem3d::Design design, double spacing, bool solve) {
    Solved s;
    s.design = std::move(design);
    fem3d::MeshSpec ms;
    ms.targetSpacing = spacing;
    auto m = fem3d::buildMesh(s.design, ms);
    if (!m) {
        return std::move(m).error();
    }
    s.mesh = std::move(m).value();
    auto a = fem3d::assemble(s.design, s.mesh);
    if (!a) {
        return std::move(a).error();
    }
    s.sys = std::move(a).value();
    if (!solve) {
        return s;
    }
    sparse::KernelPool pool(1);
    auto solver = solvers::makeSolver(solvers::SolverKind::PcgIc0);
    if (!solver) {
        return std::move(solver).error();
    }
    auto su = solver.value()->setup(s.sys.K, {}, pool, {});
    if (!su) {
        return std::move(su).error();
    }
    solvers::SolveControl c;
    c.relTol = 1e-11;
    c.maxIterations = 50000;
    s.T.assign(s.sys.f.size(), 0.0);
    auto r = solver.value()->solve(s.sys.f, s.T, c);
    if (!r) {
        return std::move(r).error();
    }
    if (r.value().status != solvers::SolveStatus::Converged) {
        return makeError(ErrorCode::InvalidInput,
                         "Figure solve did not converge: " + std::string(label(r.value().status)));
    }
    return s;
}

Rgb materialColor(const std::string& name) {
    if (name == "silicon") {
        return {0x3A, 0x3F, 0x4A};
    }
    if (name == "TIM") {
        return {0xE6, 0x9F, 0x00};
    }
    if (name == "bar") {
        return {0xB8, 0x8A, 0x5C};
    }
    return {0xC4, 0xCC, 0xD6}; // aluminium and single-material blocks
}

// Boundary surface of the mesh; colour per triangle from `faceColor` or per vertex from T.
viz::TriangleMesh surface(const Solved& s,
                          const std::function<Rgb(const fem3d::BoundaryFace&)>& faceColor,
                          double tLo = 0.0, double tHi = 0.0, const Vec3& offset = {0, 0, 0}) {
    const fem3d::SurfaceMesh sm = fem3d::extractSurface(s.mesh);
    viz::TriangleMesh out;
    out.positions.reserve(sm.vertices.size());
    for (const auto& v : sm.vertices) {
        out.positions.push_back({v[0] + offset[0], v[1] + offset[1], v[2] + offset[2]});
    }
    out.triangles = sm.triangles;
    if (faceColor) {
        out.triangleColor.reserve(sm.triangles.size());
        for (const std::int32_t f : sm.triangleFace) {
            out.triangleColor.push_back(
                faceColor(s.mesh.boundaryFaces[static_cast<std::size_t>(f)]));
        }
    } else {
        out.vertexColor.reserve(sm.vertices.size());
        for (const std::int32_t node : sm.vertexNode) {
            out.vertexColor.push_back(viz::inferno(s.T[static_cast<std::size_t>(node)], tLo, tHi));
        }
    }
    return out;
}

std::function<Rgb(const fem3d::BoundaryFace&)> byMaterial(const Solved& s) {
    return [&s](const fem3d::BoundaryFace& f) {
        const int mat = s.design.boxes[static_cast<std::size_t>(f.box)].material;
        return materialColor(s.design.materials[static_cast<std::size_t>(mat)].name);
    };
}

std::vector<viz::Segment> meshEdges(const Solved& s, const Rgb& color) {
    std::vector<viz::Segment> out;
    out.reserve(s.mesh.boundaryFaces.size() * 4);
    for (const auto& f : s.mesh.boundaryFaces) {
        const auto n = s.mesh.faceNodes(f);
        for (std::size_t k = 0; k < 4; ++k) {
            const auto& a = s.mesh.nodeXyz[static_cast<std::size_t>(n[k])];
            const auto& b = s.mesh.nodeXyz[static_cast<std::size_t>(n[(k + 1) % 4])];
            out.push_back({{a[0], a[1], a[2]}, {b[0], b[1], b[2]}, color});
        }
    }
    return out;
}

std::vector<viz::Segment> boxOutline(const fem3d::Box& b, const Rgb& color,
                                     const Vec3& off = {0, 0, 0}) {
    std::vector<viz::Segment> out;
    const auto corner = [&](int c) {
        return Vec3{((c & 1) ? b.max[0] : b.min[0]) + off[0],
                    ((c & 2) ? b.max[1] : b.min[1]) + off[1],
                    ((c & 4) ? b.max[2] : b.min[2]) + off[2]};
    };
    for (int c = 0; c < 8; ++c) {
        for (const int bit : {1, 2, 4}) {
            if ((c & bit) == 0) {
                out.push_back({corner(c), corner(c | bit), color});
            }
        }
    }
    return out;
}

std::vector<std::uint8_t> png(const viz::Image& img) {
    return viz::encodePng(img.rgba.data(), static_cast<std::uint32_t>(img.width),
                          static_cast<std::uint32_t>(img.height));
}

// Renders and places an image of view.width × view.height at (x, y), scaled by `scale`.
viz::Camera place(SvgCanvas& c, const viz::ViewSpec& view,
                  const std::vector<const viz::TriangleMesh*>& meshes,
                  const std::vector<viz::Segment>& segs, double x, double y, double scale,
                  const Vec3* lo = nullptr, const Vec3* hi = nullptr) {
    auto box = viz::emptyBox();
    if (lo != nullptr && hi != nullptr) {
        box = {*lo, *hi};
    } else {
        for (const auto* m : meshes) {
            viz::extendBox(*m, box[0], box[1]);
        }
    }
    const viz::Camera cam = viz::Camera::fit(view, box[0], box[1]);
    const viz::Image img = viz::render(cam, meshes, segs);
    c.image(png(img), x, y, view.width * scale, view.height * scale);
    return cam;
}

// Image-space point (canvas units) of a world point rendered by `cam` placed at (x, y)·scale.
std::array<double, 2> at(const viz::Camera& cam, const Vec3& p, double x, double y, double scale) {
    const Vec3 q = cam.project(p);
    return {x + q[0] * scale, y + q[1] * scale};
}

void label(SvgCanvas& c, const std::array<double, 2>& anchor, double tx, double ty,
           const std::string& text, double size = 13.0) {
    c.line(anchor[0], anchor[1], tx, ty, kInk, 0.9);
    c.circle(anchor[0], anchor[1], 2.6, kInk);
    const bool right = tx >= anchor[0];
    c.text(tx + (right ? 4.0 : -4.0), ty + 4.0, text, size, right ? "start" : "end");
}

Vec3 centre(const fem3d::Box& b) {
    return {0.5 * (b.min[0] + b.max[0]), 0.5 * (b.min[1] + b.max[1]), 0.5 * (b.min[2] + b.max[2])};
}

const fem3d::Box* boxNamed(const fem3d::Design& d, const std::string& name) {
    for (const auto& b : d.boxes) {
        if (b.name == name) {
            return &b;
        }
    }
    return nullptr;
}

// ---- Figure writers ---------------------------------------------------------------------------

struct Ctx {
    const FigureOptions& opt;
    const Results& res;
    std::vector<FigureEntry>& entries;

    Result<void> plot(const std::string& id, const std::string& file, const PlotSpec& in,
                      const std::string& sources) {
        // Every plot is printed at full page width; 820 × 430 keeps it compact and its text
        // readable (build spec §12.2).
        PlotSpec spec = in;
        if (spec.height == 500) {
            spec.height = 430;
        }
        spec.labelGutter = 64; // slope and speed-up labels sit between the lines and the legend
        if (auto r = saveSvgPlot(spec, opt.out / file); !r) {
            return r;
        }
        entries.push_back({id, file, spec.title, sources});
        return {};
    }
    Result<void> bars(const std::string& id, const std::string& file, const BarChartSpec& spec,
                      const std::string& sources) {
        if (auto r = saveSvgBars(spec, opt.out / file); !r) {
            return r;
        }
        entries.push_back({id, file, spec.title, sources});
        return {};
    }
    Result<void> canvas(const std::string& id, const std::string& file, const SvgCanvas& c,
                        const std::string& title, const std::string& sources) {
        if (auto r = c.save(opt.out / file); !r) {
            return r;
        }
        entries.push_back({id, file, title, sources});
        return {};
    }
};

void heading(SvgCanvas& c, const std::string& title, const std::string& subtitle) {
    c.text(24, 34, title, 18, "start", kInk, true);
    if (!subtitle.empty()) {
        c.text(24, 56, subtitle, 12.5, "start", kMuted);
    }
}

viz::ViewSpec isoView(int w, int h, double az = -58.0, double el = 28.0) {
    viz::ViewSpec v;
    v.width = w;
    v.height = h;
    v.azimuthDeg = az;
    v.elevationDeg = el;
    return v;
}

// P1: HS-7 assembled and exploded, materials.
Result<void> figP1(Ctx& ctx) {
    auto s = meshAndSolve(fem3d::makeHS7(), 1e-3, false);
    if (!s) {
        return std::move(s).error();
    }
    const Solved& hs = s.value();
    SvgCanvas c(1400, 640, "P1 HS-7 heat sink");
    heading(c, "HS-7: finned aluminium heat sink on a silicon die",
            "Left: assembled. Right: exploded 8 mm downwards per layer to show the TIM and the die "
            "(thickness 0.1 mm and 0.5 mm).");
    const viz::TriangleMesh body = surface(hs, byMaterial(hs));
    std::vector<viz::Segment> outline;
    for (const auto& b : hs.design.boxes) {
        auto o = boxOutline(b, {0x30, 0x34, 0x3C});
        outline.insert(outline.end(), o.begin(), o.end());
    }
    const viz::ViewSpec v = isoView(1300, 1060, -58.0, 24.0);
    const double scale = 0.5;
    const viz::Camera cam = place(c, v, {&body}, outline, 20, 70, scale);

    // Exploded: separate meshes per layer, shifted down.
    const auto layer = [&](const std::string& material, const Vec3& off) {
        viz::TriangleMesh m = surface(
            hs,
            [&](const fem3d::BoundaryFace& f) {
                return materialColor(
                    hs.design
                        .materials[static_cast<std::size_t>(
                            hs.design.boxes[static_cast<std::size_t>(f.box)].material)]
                        .name);
            },
            0.0, 0.0, off);
        // Keep only the triangles of this material.
        const fem3d::SurfaceMesh sm = fem3d::extractSurface(hs.mesh);
        std::vector<std::array<std::int32_t, 3>> tri;
        std::vector<Rgb> col;
        for (std::size_t t = 0; t < sm.triangles.size(); ++t) {
            const auto& f = hs.mesh.boundaryFaces[static_cast<std::size_t>(sm.triangleFace[t])];
            const int mat = hs.design.boxes[static_cast<std::size_t>(f.box)].material;
            if (hs.design.materials[static_cast<std::size_t>(mat)].name == material) {
                tri.push_back(sm.triangles[t]);
                col.push_back(m.triangleColor[t]);
            }
        }
        m.triangles = std::move(tri);
        m.triangleColor = std::move(col);
        return m;
    };
    const double gap = 0.008;
    const viz::TriangleMesh al = layer("aluminium", {0, 0, 0});
    const viz::TriangleMesh tim = layer("TIM", {0, 0, -gap});
    const viz::TriangleMesh die = layer("silicon", {0, 0, -2.0 * gap});
    // TIM and die faces are interior to the mesh where they touch other boxes; draw them as
    // closed boxes instead so the exploded layers look solid.
    const auto solidBox = [&](const fem3d::Box& b, const Rgb& color, const Vec3& off) {
        viz::TriangleMesh m;
        for (int corner = 0; corner < 8; ++corner) {
            m.positions.push_back({((corner & 1) ? b.max[0] : b.min[0]) + off[0],
                                   ((corner & 2) ? b.max[1] : b.min[1]) + off[1],
                                   ((corner & 4) ? b.max[2] : b.min[2]) + off[2]});
        }
        const int quads[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4},
                                 {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
        for (const auto& q : quads) {
            m.triangles.push_back({q[0], q[1], q[2]});
            m.triangles.push_back({q[0], q[2], q[3]});
            m.triangleColor.push_back(color);
            m.triangleColor.push_back(color);
        }
        return m;
    };
    std::vector<viz::TriangleMesh> solids;
    std::vector<viz::Segment> outline2;
    for (const auto& b : hs.design.boxes) {
        const std::string mat = hs.design.materials[static_cast<std::size_t>(b.material)].name;
        const Vec3 off = mat == "TIM" ? Vec3{0, 0, -gap}
                                      : (mat == "silicon" ? Vec3{0, 0, -2.0 * gap} : Vec3{0, 0, 0});
        if (mat != "aluminium") {
            solids.push_back(solidBox(b, materialColor(mat), off));
        }
        auto o = boxOutline(b, {0x30, 0x34, 0x3C}, off);
        outline2.insert(outline2.end(), o.begin(), o.end());
    }
    std::vector<const viz::TriangleMesh*> parts{&al};
    for (const auto& m : solids) {
        parts.push_back(&m);
    }
    (void)tim;
    (void)die;
    auto box = viz::emptyBox();
    for (const auto* m : parts) {
        viz::extendBox(*m, box[0], box[1]);
    }
    const viz::Camera cam2 = place(c, v, parts, outline2, 700, 70, scale, &box[0], &box[1]);
    const fem3d::Box* base = boxNamed(hs.design, "base");
    const fem3d::Box* fin = boxNamed(hs.design, "fin 1");
    if (base != nullptr && fin != nullptr) {
        label(c, at(cam, {0.75 * base->max[0], 0.0, 0.5 * base->max[2]}, 20, 70, scale), 470, 610,
              "base 60 × 40 × 4 mm, aluminium, k = 200 W/(m·K)", 12.5);
        label(c, at(cam, {fin->min[0], fin->min[1], fin->max[2]}, 20, 70, scale), 150, 92,
              "7 fins 2 × 40 × 26 mm, pitch 9 mm", 12.5);
    }
    const fem3d::Box* timBox = boxNamed(hs.design, "TIM");
    const fem3d::Box* dieBox = boxNamed(hs.design, "die");
    if (timBox != nullptr && dieBox != nullptr) {
        Vec3 t = centre(*timBox);
        t[0] = timBox->max[0];
        t[2] -= gap;
        Vec3 d = centre(*dieBox);
        d[0] = dieBox->max[0];
        d[2] -= 2.0 * gap;
        label(c, at(cam2, t, 700, 70, scale), 1170, 585, "TIM 0.1 mm, k = 3 W/(m·K)", 12.5);
        label(c, at(cam2, d, 700, 70, scale), 1170, 615, "die 0.5 mm, silicon, 30 W", 12.5);
    }
    return ctx.canvas("P1", "P1_hs7_render.svg", c, "HS-7 assembled and exploded (materials)",
                      "fem3d::makeHS7, mesh s = 1 mm");
}

// P2: boundary conditions on the surface.
Result<void> figP2(Ctx& ctx) {
    auto s = meshAndSolve(fem3d::makeHS7(), 1e-3, false);
    if (!s) {
        return std::move(s).error();
    }
    const Solved& hs = s.value();
    const Rgb conv{0x9A, 0xD2, 0xF2}, ins{0xB0, 0xB4, 0xBA};
    const auto bcColor = [&](const fem3d::BoundaryFace& f) {
        const auto& bc = hs.design.boxes[static_cast<std::size_t>(f.box)]
                             .faces[static_cast<std::size_t>(f.side)];
        const int mat = hs.design.boxes[static_cast<std::size_t>(f.box)].material;
        if (hs.design.materials[static_cast<std::size_t>(mat)].name == "silicon") {
            return Rgb{0x3A, 0x3F, 0x4A}; // the heat source (its faces are insulated)
        }
        return bc.kind == fem3d::BcKind::Convection ? conv : ins;
    };
    SvgCanvas c(1400, 590, "P2 boundary conditions");
    heading(
        c, "Boundary conditions and load",
        "Every exposed aluminium face convects to air; the board side (base bottom, die and TIM) "
        "is insulated, so all 30 W leave through the fins and the base.");
    const viz::TriangleMesh top = surface(hs, bcColor);
    const viz::ViewSpec v1 = isoView(1100, 900, -58.0, 26.0);
    place(c, v1, {&top}, {}, 10, 80, 0.5);
    const viz::ViewSpec v2 = isoView(1100, 900, -58.0, -32.0);
    place(c, v2, {&top}, {}, 570, 80, 0.5);
    c.text(285, 560, "seen from above", 12.5, "middle", kMuted);
    c.text(845, 560, "seen from below (board side)", 12.5, "middle", kMuted);
    const double lx = 1150;
    c.rect(lx, 150, 22, 16, viz::toHex(conv));
    c.text(lx + 30, 163, "convection", 13);
    c.text(lx + 30, 181, "h = 25 W/(m²·K), T∞ = 25 °C", 12, "start", kMuted);
    c.rect(lx, 210, 22, 16, viz::toHex(ins));
    c.text(lx + 30, 223, "insulated", 13);
    c.text(lx + 30, 241, "board side of base and TIM", 12, "start", kMuted);
    c.rect(lx, 270, 22, 16, "#3A3F4A");
    c.text(lx + 30, 283, "heat source (die)", 13);
    c.text(lx + 30, 301, "P = 30 W, faces insulated", 12, "start", kMuted);
    c.text(lx, 350, "Steady conduction:", 12.5, "start", kInk, true);
    c.text(lx, 372, "−∇·(k∇T) = q", 12, "start", kInk, false, true);
    c.text(lx, 388, "   in each material", 11.5, "start", kMuted);
    c.text(lx, 410, "−k ∂T/∂n = h(T − T∞)", 12, "start", kInk, false, true);
    c.text(lx, 426, "   on faces in air", 11.5, "start", kMuted);
    c.text(lx, 448, "−k ∂T/∂n = 0", 12, "start", kInk, false, true);
    c.text(lx, 464, "   on the board side", 11.5, "start", kMuted);
    return ctx.canvas("P2", "P2_boundary_conditions.svg", c, "Boundary conditions and load",
                      "fem3d::makeHS7 face conditions");
}

// P3: meshes at s = 1.0 and 0.5 mm and the TIM detail.
Result<void> figP3(Ctx& ctx) {
    SvgCanvas c(1400, 900, "P3 meshes");
    heading(c, "Graded hexahedral meshes of HS-7",
            "Tensor grid with a cell mask, neighbour size ratio ≤ 1.5; one cell across the TIM.");
    const Rgb edge{0x30, 0x34, 0x3C};
    double y = 70;
    for (const double spacing : {1e-3, 5e-4}) {
        auto s = meshAndSolve(fem3d::makeHS7(), spacing, false);
        if (!s) {
            return std::move(s).error();
        }
        const Solved& hs = s.value();
        const viz::TriangleMesh body = surface(hs, byMaterial(hs));
        const auto edges = meshEdges(hs, edge);
        viz::ViewSpec v = isoView(1300, 760, -58.0, 26.0);
        v.lineWidth = 0.6;
        place(c, v, {&body}, edges, 20, y, 0.5);
        c.text(345, y + 395,
               "s = " + fmt(spacing * 1e3, "%.1f") +
                   " mm: " + fmt(static_cast<double>(hs.mesh.stats.nodes), "%.0f") + " nodes, " +
                   fmt(static_cast<double>(hs.mesh.stats.activeCells), "%.0f") +
                   " cells, max aspect ratio " + fmt(hs.mesh.stats.maxAspect, "%.1f"),
               12.5, "middle", kMuted);
        if (spacing < 0.9e-3) {
            // TIM detail: a corner of the die, from below.
            const fem3d::Box* dieBox = boxNamed(hs.design, "die");
            if (dieBox != nullptr) {
                const Vec3 lo{dieBox->min[0] - 2.0e-3, dieBox->min[1] - 2.0e-3, dieBox->min[2]};
                const Vec3 hi{dieBox->min[0] + 2.0e-3, dieBox->min[1] + 2.0e-3, 1.0e-3};
                viz::ViewSpec dv = isoView(1000, 1000, -58.0, -24.0);
                dv.lineWidth = 1.2;
                // Crop: keep triangles inside the detail box.
                viz::TriangleMesh crop = body;
                std::vector<std::array<std::int32_t, 3>> tri;
                std::vector<Rgb> col;
                for (std::size_t t = 0; t < crop.triangles.size(); ++t) {
                    bool inside = true;
                    for (const auto id : crop.triangles[t]) {
                        const Vec3& p = crop.positions[static_cast<std::size_t>(id)];
                        for (std::size_t k = 0; k < 3; ++k) {
                            inside = inside && p[k] >= lo[k] - 1e-12 && p[k] <= hi[k] + 1e-12;
                        }
                    }
                    if (inside) {
                        tri.push_back(crop.triangles[t]);
                        col.push_back(crop.triangleColor[t]);
                    }
                }
                crop.triangles = std::move(tri);
                crop.triangleColor = std::move(col);
                std::vector<viz::Segment> cropEdges;
                for (const auto& e : edges) {
                    bool inside = true;
                    for (const Vec3* p : {&e.a, &e.b}) {
                        for (std::size_t k = 0; k < 3; ++k) {
                            inside = inside && (*p)[k] >= lo[k] - 1e-12 && (*p)[k] <= hi[k] + 1e-12;
                        }
                    }
                    if (inside) {
                        cropEdges.push_back(e);
                    }
                }
                place(c, dv, {&crop}, cropEdges, 860, 110, 0.5, &lo, &hi);
                c.rect(860, 110, 500, 500, "none", kMuted, 0.6);
                c.text(1110, 635, "Detail at s = 0.5 mm: a corner of the die seen from below", 12,
                       "middle", kMuted);
                c.text(1110, 651, "(die dark, TIM orange: one cell across 0.1 mm)", 12, "middle",
                       kMuted);
            }
        }
        y += 420;
    }
    return ctx.canvas("P3", "P3_meshes.svg", c, "Meshes at s = 1.0 and 0.5 mm, TIM detail",
                      "fem3d::buildMesh(makeHS7, s)");
}

// P4: surface temperature, two views.
Result<void> figP4(Ctx& ctx, const Solved& hs) {
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (const double t : hs.T) {
        lo = std::min(lo, t);
        hi = std::max(hi, t);
    }
    SvgCanvas c(1400, 640, "P4 temperature field");
    heading(c, "Steady temperature on the surface of HS-7 (s = 0.5 mm)",
            "P = 30 W, h = 25 W/(m²·K), T∞ = 25 °C. Peak " + fmt(hi, "%.2f") +
                " °C in the die; the "
                "fins span only a few kelvin.");
    const viz::TriangleMesh field = surface(hs, nullptr, lo, hi);
    place(c, isoView(1100, 900, -58.0, 26.0), {&field}, {}, 20, 70, 0.55);
    place(c, isoView(1100, 900, -58.0, -32.0), {&field}, {}, 620, 70, 0.55);
    c.text(320, 600, "from above", 12.5, "middle", kMuted);
    c.text(920, 600, "from below: the die is the hot spot", 12.5, "middle", kMuted);
    c.colorbar(1260, 120, 24, 400, lo, hi, "T (°C)", 6);
    return ctx.canvas("P4", "P4_temperature_field.svg", c, "Surface temperature field, two views",
                      "HS-7 at s = 0.5 mm, PCG-IC0 to 1e-11 (same system as E2-HS7-s0.500)");
}

// Raster of T on a plane, sampled per pixel (background outside the body).
viz::Image planeImage(const Solved& hs, int axis, double value, double uLo, double uHi, double vLo,
                      double vHi, int width, int height, double tLo, double tHi) {
    viz::Image img(width, height, {0xF2, 0xF4, 0xF7});
    // Plane axes: u and v are the two other axes, v upwards.
    const int ua = axis == 0 ? 1 : 0;
    const int va = axis == 2 ? 1 : 2;
    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            const double u = uLo + (px + 0.5) / width * (uHi - uLo);
            const double v = vHi - (py + 0.5) / height * (vHi - vLo);
            double p[3];
            p[axis] = value;
            p[ua] = u;
            p[va] = v;
            const auto t = fem3d::sampleTemperature(hs.mesh, hs.T, p[0], p[1], p[2]);
            if (t) {
                img.set(px, py, viz::inferno(t.value(), tLo, tHi));
            }
        }
    }
    return img;
}

// P5: cut planes and fin profile vs 1-D fin theory.
Result<void> figP5(Ctx& ctx, const Solved& hs) {
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (const double t : hs.T) {
        lo = std::min(lo, t);
        hi = std::max(hi, t);
    }
    const fem3d::HeatSinkParams p;
    SvgCanvas c(1400, 530, "P5 cut planes");
    heading(c, "Cut planes through the die and the middle fin (s = 0.5 mm)",
            "Left: plane y = 20 mm (across the fins). Right: plane through the middle of fin 4. "
            "Vertical scale = horizontal scale.");
    const double zLo = -(p.timThickness + p.dieZ), zHi = p.baseZ + p.finHeight;
    // Plane y = mid: x from 0 to 60 mm, z from −0.6 to 30 mm.
    const int w1 = 640;
    const int h1 = static_cast<int>(std::lround(w1 * (zHi - zLo) / p.baseX));
    const viz::Image a = planeImage(hs, 1, 0.5 * p.baseY, 0.0, p.baseX, zLo, zHi, w1, h1, lo, hi);
    c.image(png(a), 30, 90, w1, h1);
    c.rect(30, 90, w1, h1, "none", kMuted, 0.6);
    c.text(30 + w1 / 2.0, 90 + h1 + 22, "x (0 … 60 mm)", 12, "middle", kMuted);
    // Plane x = middle of fin 4: y from 0 to 40 mm.
    const double xFin = p.finFirstX + 3.0 * p.finPitch + 0.5 * p.finThickness;
    const int w2 = static_cast<int>(std::lround(h1 * p.baseY / (zHi - zLo)));
    const viz::Image b = planeImage(hs, 0, xFin, 0.0, p.baseY, zLo, zHi, w2, h1, lo, hi);
    c.image(png(b), 710, 90, w2, h1);
    c.rect(710, 90, w2, h1, "none", kMuted, 0.6);
    c.text(710 + w2 / 2.0, 90 + h1 + 22, "y (0 … 40 mm), x = " + fmt(xFin * 1e3, "%.0f") + " mm",
           12, "middle", kMuted);
    c.colorbar(1200, 110, 22, 300, lo, hi, "T (°C)", 6);
    c.textBlock(30, 90 + h1 + 60,
                "The die (bottom, under the base centre) is the hottest part; the base spreads the "
                "heat sideways\nbefore it enters the fins, so the middle fins run hotter than the "
                "outer ones.",
                12.5, 1.4, kMuted);
    if (auto r = ctx.canvas("P5", "P5_cut_planes.svg", c, "Cut planes through die and fin",
                            "HS-7 at s = 0.5 mm, fem3d::sampleTemperature");
        !r) {
        return r;
    }

    // Fin profile: T along the middle fin's centre line vs 1-D fin theory (convective tip) with
    // the FE root temperature as T_b.
    std::vector<double> zs, fe, th;
    const auto tb = fem3d::sampleTemperature(hs.mesh, hs.T, xFin, 0.5 * p.baseY, p.baseZ);
    if (!tb) {
        return std::move(tb).error();
    }
    const double k = p.kAluminium, h = p.h, t = p.finThickness, W = p.baseY, L = p.finHeight;
    const double P = 2.0 * (W + t), A = W * t;
    const double m = std::sqrt(h * P / (k * A)), ratio = h / (m * k);
    for (int i = 0; i <= 52; ++i) {
        const double x = L * i / 52.0;
        const auto tf = fem3d::sampleTemperature(hs.mesh, hs.T, xFin, 0.5 * p.baseY, p.baseZ + x);
        if (!tf) {
            continue;
        }
        zs.push_back(x * 1e3);
        fe.push_back(tf.value());
        th.push_back(p.tInf + (tb.value() - p.tInf) *
                                  (std::cosh(m * (L - x)) + ratio * std::sinh(m * (L - x))) /
                                  (std::cosh(m * L) + ratio * std::sinh(m * L)));
    }
    PlotSpec spec;
    spec.title = "Middle fin: FE temperature vs 1-D fin theory";
    spec.xLabel = "height above the base (mm)";
    spec.yLabel = "T (°C)";
    PlotSeries s1;
    s1.label = "FE, centre line of fin 4";
    s1.x = zs;
    s1.y = fe;
    s1.color = kAmg;
    s1.marker = PlotMarker::Circle;
    spec.series = {s1, reference("1-D fin theory, T_b from FE", zs, th)};
    spec.note = "fin theory with a convective tip, m = sqrt(hP/kA) (Bergman et al. 2011); 1-D "
                "theory ignores "
                "the spread in y";
    return ctx.plot("P5", "P5_fin_profile.svg", spec, "HS-7 at s = 0.5 mm, fin 4 centre line");
}

// P6: v1 bar in 3D and centre line vs v1 parabola.
Result<void> figP6(Ctx& ctx) {
    auto s = meshAndSolve(fem3d::makeV1Bar(), 0.005, true);
    if (!s) {
        return std::move(s).error();
    }
    const Solved& bar = s.value();
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (const double t : bar.T) {
        lo = std::min(lo, t);
        hi = std::max(hi, t);
    }
    SvgCanvas c(1400, 330, "P6 v1 bar in 3D");
    heading(c, "v1's bar solved in 3D (s = 5 mm)",
            "1 m long, 10 × 10 mm, k = 10 W/(m·K), q = 10 kW/m³, ends held at 100 °C and 20 °C, "
            "sides insulated.");
    const viz::TriangleMesh field = surface(bar, nullptr, lo, hi);
    viz::ViewSpec v = isoView(2400, 420, -80.0, 22.0);
    place(c, v, {&field}, meshEdges(bar, {0x30, 0x34, 0x3C}), 20, 80, 0.5);
    c.colorbar(1290, 90, 20, 190, lo, hi, "T (°C)", 4);
    if (auto r = ctx.canvas("P6", "P6_v1_bar_render.svg", c, "v1 bar in 3D",
                            "fem3d::makeV1Bar at s = 5 mm (as E1-verify-bar)");
        !r) {
        return r;
    }
    std::vector<double> xs, fe, ex;
    for (int i = 0; i <= 100; ++i) {
        const double x = i / 100.0;
        const auto t = fem3d::sampleTemperature(bar.mesh, bar.T, x, 0.0, 0.0);
        if (t) {
            xs.push_back(x);
            fe.push_back(t.value());
            ex.push_back(100.0 + 420.0 * x - 500.0 * x * x);
        }
    }
    PlotSpec spec;
    spec.title = "Centre line of the 3D bar vs v1's exact parabola";
    spec.xLabel = "x (m)";
    spec.yLabel = "T (°C)";
    PlotSeries a;
    a.label = "3D FE (centre line)";
    a.x = xs;
    a.y = fe;
    a.color = kAmg;
    a.line = false;
    a.marker = PlotMarker::Circle;
    spec.series = {a, reference("exact: 100 + 420x − 500x²", xs, ex)};
    const Row* e1 = ctx.res.find("E1-verify-bar");
    spec.annotations = {
        {0.42, 188.2,
         "peak 188.2 °C at x = 0.42 m; max nodal error " +
             (e1 != nullptr ? fmt(num(*e1, "check_error"), "%.1e") : std::string("?")) + " K",
         kInk, 8.0, 16.0}};
    spec.note = "data: results/v2/csv/E1.csv (E1-verify-bar)";
    return ctx.plot("P6", "P6_v1_bar_profile.svg", spec, "E1-verify-bar");
}

// P7: manufactured-solution convergence.
Result<void> figP7(Ctx& ctx) {
    std::vector<double> h, rms, mx;
    for (const int n : {4, 8, 16, 32}) {
        const Row* r = ctx.res.find("E1-verify-mms" + std::to_string(n));
        if (r == nullptr) {
            continue;
        }
        h.push_back(20.0 / n);
        rms.push_back(num(*r, "check_fe"));
        mx.push_back(num(*r, "check_error"));
    }
    if (h.size() < 2) {
        return makeError(ErrorCode::InvalidInput, "P7 needs the E1 manufactured-solution rows");
    }
    PlotSpec spec;
    spec.title = "Manufactured solution: error vs cell size";
    spec.xLabel = "cell size h (mm)";
    spec.yLabel = "error (K)";
    spec.logX = spec.logY = true;
    spec.powerTicks = true;
    PlotSeries a;
    a.label = "RMS error";
    a.x = h;
    a.y = rms;
    a.color = kAmg;
    a.marker = PlotMarker::Circle;
    PlotSeries b;
    b.label = "max nodal error";
    b.x = h;
    b.y = mx;
    b.color = kDirect;
    b.marker = PlotMarker::Square;
    b.dashed = true;
    std::vector<double> ref;
    for (const double v : h) {
        ref.push_back(rms.front() * (v / h.front()) * (v / h.front()));
    }
    spec.series = {a, b, reference("slope 2", h, ref)};
    spec.annotations = {
        {h.back(), rms.back(), "slope " + fmt(logSlope(h, rms), "%.2f"), kAmg, 8.0, 14.0},
        {h.back(), mx.back(), "slope " + fmt(logSlope(h, mx), "%.2f"), kDirect, 8.0, -8.0}};
    spec.note = "data: results/v2/csv/E1.csv (E1-verify-mms4 … mms32)";
    return ctx.plot("P7", "P7_mms_convergence.svg", spec, "E1-verify-mms4/8/16/32");
}

// P8: sparsity of K and of L under three orderings.
Result<void> figP8(Ctx& ctx) {
    auto s = meshAndSolve(fem3d::makeHS7(), 2e-3, false);
    if (!s) {
        return std::move(s).error();
    }
    const auto& K = s.value().sys.K;
    SvgCanvas c(1400, 470, "P8 sparsity patterns");
    heading(
        c, "Sparsity of K and of the Cholesky factor L under three orderings (HS-7, s = 2 mm)",
        "Density rasters: darker = more entries per block. Fill-in is the area L covers beyond K.");
    viz::SpyOptions so;
    so.pixels = 300;
    so.color = viz::hexColor(kInk);
    const auto ptr = [](const std::vector<sparse::Offset>& v) {
        return std::vector<std::int64_t>(v.begin(), v.end());
    };
    const viz::Image k = viz::renderSpy(K.rows, ptr(K.rowPtr), K.col, false, so);
    c.image(png(k), 24, 100, 300, 300);
    c.rect(24, 100, 300, 300, "none", kMuted, 0.8);
    c.text(174, 425, "K (grid order): nnz = " + fmt(static_cast<double>(K.nnz()), "%.0f"), 12.5,
           "middle");
    double x = 370;
    for (const auto& [ordering, name] : std::vector<std::pair<solvers::Ordering, std::string>>{
             {solvers::Ordering::Natural, "natural"},
             {solvers::Ordering::Rcm, "RCM"},
             {solvers::Ordering::NestedDissection, "nested dissection"}}) {
        solvers::DirectCholesky d(solvers::SolverKind::DirectND, ordering);
        sparse::KernelPool pool(1);
        auto su = d.setup(K, {}, pool, {});
        if (!su) {
            return std::move(su).error();
        }
        const auto& L = d.factor();
        viz::SpyOptions lo = so;
        lo.color = viz::hexColor(kDirect);
        const viz::Image img = viz::renderSpy(L.n, L.colPtr, L.rowIdx, true, lo);
        c.image(png(img), x, 100, 300, 300);
        c.rect(x, 100, 300, 300, "none", kMuted, 0.8);
        c.text(x + 150, 425,
               "L, " + name + ": nnz = " + fmt(static_cast<double>(su.value().factorNnz), "%.3g"),
               12.5, "middle");
        x += 340;
    }
    c.text(24, 455,
           "n = " + fmt(static_cast<double>(K.rows), "%.0f") +
               " DOF. L is stored as its lower triangle; nnz includes the diagonal.",
           11.5, "start", kMuted);
    return ctx.canvas("P8", "P8_spy.svg", c, "Sparsity of K and L (natural, RCM, ND)",
                      "HS-7 at s = 2 mm, solvers::DirectCholesky::factor()");
}

// P9: AMG aggregates on the surface + hierarchy table.
Result<void> figP9(Ctx& ctx) {
    auto s = meshAndSolve(fem3d::makeHS7(), 1e-3, false);
    if (!s) {
        return std::move(s).error();
    }
    const Solved& hs = s.value();
    const solvers::SolverOptions opt;
    sparse::KernelPool pool(1);
    solvers::AmgHierarchy h;
    auto rep = h.build(hs.sys.K, pool, opt);
    if (!rep) {
        return std::move(rep).error();
    }
    const auto& agg = h.levelCount() > 1 ? h.aggregates(0) : std::vector<sparse::Index>{};
    std::size_t isolated = 0;
    for (const auto a : agg) {
        isolated += a < 0 ? 1U : 0U;
    }
    const fem3d::SurfaceMesh sm = fem3d::extractSurface(hs.mesh);
    viz::TriangleMesh m;
    for (const auto& v : sm.vertices) {
        m.positions.push_back({v[0], v[1], v[2]});
    }
    m.triangles = sm.triangles;
    for (const auto& tri : sm.triangles) {
        const auto node = sm.vertexNode[static_cast<std::size_t>(tri[0])];
        const sparse::Index a =
            static_cast<std::size_t>(node) < agg.size() ? agg[static_cast<std::size_t>(node)] : -1;
        m.triangleColor.push_back(a < 0 ? Rgb{0xFF, 0xFF, 0xFF}
                                        : viz::categorical(static_cast<std::size_t>(a) * 7U));
    }
    SvgCanvas c(1400, 640, "P9 AMG aggregates");
    heading(c, "Smoothed-aggregation AMG: level-0 aggregates on the surface (HS-7, s = 1 mm)",
            "Each colour is one aggregate (one coarse unknown). White: nodes with no strong "
            "connection, left out of the coarse space.");
    place(c, isoView(1100, 900, -58.0, 26.0), {&m}, {}, 20, 70, 0.6);
    double y = 110;
    c.text(720, y, "Hierarchy", 14, "start", kInk, true);
    y += 26;
    c.text(720, y, "level", 12, "start", kMuted);
    c.text(800, y, "rows", 12, "end", kMuted);
    c.text(900, y, "entries", 12, "end", kMuted);
    c.text(1000, y, "rows/row₀", 12, "end", kMuted);
    const auto& levels = rep.value().levels;
    for (std::size_t l = 0; l < levels.size(); ++l) {
        y += 20;
        c.text(720, y, std::to_string(l), 12, "start", kInk, false, true);
        c.text(800, y, fmt(static_cast<double>(levels[l].first), "%.0f"), 12, "end", kInk, false,
               true);
        c.text(900, y, fmt(static_cast<double>(levels[l].second), "%.0f"), 12, "end", kInk, false,
               true);
        c.text(1000, y,
               fmt(static_cast<double>(levels[l].first) / static_cast<double>(levels[0].first),
                   "%.4f"),
               12, "end", kInk, false, true);
    }
    y += 32;
    c.text(720, y,
           "operator complexity " + fmt(rep.value().operatorComplexity, "%.3f") +
               ", grid complexity " + fmt(rep.value().gridComplexity, "%.3f"),
           12.5);
    y += 20;
    c.text(720, y,
           "strength threshold θ₀ = " + fmt(opt.amgStrengthThreshold, "%.3g") +
               ", halved per level",
           12.5);
    y += 20;
    c.text(720, y,
           "level 0: " + fmt(static_cast<double>(isolated), "%.0f") + " of " +
               fmt(static_cast<double>(agg.size()), "%.0f") + " nodes isolated (" +
               fmt(100.0 * static_cast<double>(isolated) /
                       std::max<double>(1.0, static_cast<double>(agg.size())),
                   "%.1f") +
               " %)",
           12.5);
    return ctx.canvas("P9", "P9_amg_aggregates.svg", c, "AMG aggregates and hierarchy",
                      "HS-7 at s = 1 mm, solvers::AmgHierarchy with the default options");
}

// P10: residual vs iteration on HS-7 at 0.5 mm.
Result<void> figP10(Ctx& ctx) {
    PlotSpec spec;
    spec.title = "Convergence on HS-7 at s = 0.5 mm (286 k DOF)";
    spec.xLabel = "iteration";
    spec.yLabel = "relative residual ‖r‖/‖f‖";
    spec.logY = true;
    spec.powerTicks = true;
    for (const std::string solver : {"PCG-J", "PCG-IC0", "AMG", "AMG-PCG"}) {
        const auto hst = history(ctx.res, "E2-HS7-s0.500-" + solver, "iteration", "rel_residual");
        if (hst.empty()) {
            continue;
        }
        PlotSeries s = styled(solver);
        s.marker = PlotMarker::None;
        for (const auto& [x, y] : hst) {
            s.x.push_back(x);
            s.y.push_back(y);
        }
        spec.series.push_back(s);
    }
    if (spec.series.empty()) {
        return makeError(ErrorCode::InvalidInput,
                         "P10 needs results/v2/history/E2-HS7-s0.500-*.csv");
    }
    spec.note = "data: results/v2/history/E2-HS7-s0.500-<solver>.csv (first repetition)";
    return ctx.plot("P10", "P10_residual_history.svg", spec,
                    "E2-HS7-s0.500-{PCG-J,PCG-IC0,AMG,AMG-PCG}");
}

// E2 sweep of one quantity per solver; failed runs as hollow crosses.
PlotSpec sweepPlot(const Results& res, const std::string& title, const std::string& yLabel,
                   const std::function<double(const Row&)>& y, bool slopes,
                   const std::function<double(const Row&)>& failY, bool labelsBelow = false) {
    PlotSpec spec;
    spec.title = title;
    spec.xLabel = "DOF";
    spec.yLabel = yLabel;
    spec.logX = spec.logY = true;
    spec.powerTicks = true;
    spec.powerTicks = true;
    std::map<double, std::map<std::string, std::vector<std::string>>>
        failNotes; // dof → reason → solvers
    std::map<double, double> failAt;
    PlotSeries fails;
    fails.label = "not finished (reason)";
    fails.color = kInk;
    fails.line = false;
    fails.marker = PlotMarker::Cross;
    for (const std::string& solver : solverOrder()) {
        auto rows = res.where([&](const Row& r) {
            return str(r, "experiment") == "E2" && str(r, "solver") == solver &&
                   str(r, "role") == "solve" && str(r, "id").find("noobs") == std::string::npos;
        });
        std::sort(rows.begin(), rows.end(),
                  [](const Row* a, const Row* b) { return num(*a, "dof") < num(*b, "dof"); });
        PlotSeries s = styled(solver);
        for (const Row* r : rows) {
            const double v = y(*r);
            if (converged(*r) && std::isfinite(v) && v > 0.0) {
                s.x.push_back(num(*r, "dof"));
                s.y.push_back(v);
            } else if (failY) {
                const double fy = failY(*r);
                if (std::isfinite(fy) && fy > 0.0) {
                    const double dof = num(*r, "dof");
                    if (failAt.count(dof) == 0) {
                        failAt[dof] = fy;
                        fails.x.push_back(dof);
                        fails.y.push_back(fy);
                    }
                    failNotes[dof][shortReason(*r)].push_back(solver);
                }
            }
        }
        if (s.x.empty()) {
            continue;
        }
        if (slopes && s.x.size() >= 3) {
            const double dy = -6.0 + 13.0 * static_cast<double>(spec.series.size() % 3);
            spec.annotations.push_back({s.x.back(), s.y.back(),
                                        "slope " + fmt(logSlope(s.x, s.y), "%.2f"), s.color, 7.0,
                                        dy});
        }
        spec.series.push_back(s);
    }
    if (!fails.x.empty()) {
        spec.series.push_back(fails);
    }
    // Each reason once, at the first cross where it occurs, naming every solver that failed for
    // that reason at any size; further crosses are explained by the legend and the caption. The
    // label goes beside the cross, away from the curves (above, or below when the crosses sit
    // at the top of the plot).
    std::map<std::string, std::vector<std::string>> whoByReason;
    for (const auto& [dof, byReason] : failNotes) {
        for (const auto& [reason, names] : byReason) {
            auto& all = whoByReason[reason];
            for (const auto& n : names) {
                if (std::find(all.begin(), all.end(), n) == all.end()) {
                    all.push_back(n);
                }
            }
        }
    }
    std::set<std::string> labelled;
    for (const auto& [dof, byReason] : failNotes) {
        double dy = labelsBelow ? 16.0 : -10.0;
        for (const auto& [reason, names] : byReason) {
            if (!labelled.insert(reason).second) {
                continue;
            }
            std::string who;
            for (const auto& n : whoByReason[reason]) {
                who += (who.empty() ? "" : ", ") + n;
            }
            spec.annotations.push_back(
                {dof, failAt[dof], who + ": " + reason, kMuted, 8.0, dy, "start"});
            dy += labelsBelow ? 16.0 : -16.0;
        }
    }
    return spec;
}

Result<void> figP11(Ctx& ctx) {
    const auto failTime = [&](const Row& r) {
        // Draw a refused run at the time of the slowest finished run of its size (a marker of
        // "would take longer"), so the cross sits at the right DOF.
        double t = kNaN;
        for (const Row* o : ctx.res.where([&](const Row& q) {
                 return str(q, "experiment") == "E2" && num(q, "dof") == num(r, "dof") &&
                        converged(q);
             })) {
            t = std::isfinite(t) ? std::max(t, totalSeconds(*o)) : totalSeconds(*o);
        }
        return std::isfinite(t) ? t * 2.0 : kNaN;
    };
    PlotSpec a = sweepPlot(ctx.res, "Time to solve HS-7 once vs problem size (setup + solve)",
                           "time (s)", totalSeconds, true, failTime);
    a.note = "data: results/v2/csv/E2.csv; single thread; medians of 5/3/1 repetitions; crosses: "
             "not run";
    if (auto r = ctx.plot("P11", "P11_time_vs_dof.svg", a, "results/v2/csv/E2.csv"); !r) {
        return r;
    }
    PlotSpec b = sweepPlot(
        ctx.res, "Setup time vs problem size", "setup time (s)",
        [](const Row& r) { return num(r, "setup_s"); }, true, nullptr);
    b.note =
        "data: results/v2/csv/E2.csv (setup_s: ordering + factorisation, IC(0), AMG hierarchy)";
    if (auto r = ctx.plot("P11", "P11b_setup_vs_dof.svg", b, "results/v2/csv/E2.csv"); !r) {
        return r;
    }
    PlotSpec c = sweepPlot(
        ctx.res, "Solve time vs problem size (one load case)", "solve time (s)",
        [](const Row& r) { return num(r, "solve_s_median"); }, true, nullptr);
    c.note = "data: results/v2/csv/E2.csv (solve_s_median)";
    return ctx.plot("P11", "P11c_solve_vs_dof.svg", c, "results/v2/csv/E2.csv");
}

Result<void> figP12(Ctx& ctx) {
    const auto gb = [](const Row& r) { return num(r, "peak_ws_bytes") / 1e9; };
    const auto predicted = [](const Row& r) {
        // Refused direct runs: the factor the symbolic analysis predicted (8 + 4 bytes/entry).
        const double nnz = num(r, "predicted_factor_nnz");
        return nnz > 0.0 ? nnz * 12.0 / 1e9 : kNaN;
    };
    PlotSpec spec = sweepPlot(ctx.res, "Peak memory vs problem size", "peak working set (GB)", gb,
                              true, predicted, true);
    std::vector<double> xs;
    for (const auto& s : spec.series) {
        xs.insert(xs.end(), s.x.begin(), s.x.end());
    }
    if (!xs.empty()) {
        const auto [mn, mxIt] = std::minmax_element(xs.begin(), xs.end());
        const double budget = static_cast<double>(std::size_t(8) << 30) / 1e9;
        spec.series.push_back(
            reference("8 GiB direct-solver budget", {*mn, *mxIt}, {budget, budget}));
    }
    spec.note = "data: results/v2/csv/E2.csv (peak_ws_bytes); crosses: predicted factor size of "
                "refused runs";
    return ctx.plot("P12", "P12_memory_vs_dof.svg", spec, "results/v2/csv/E2.csv");
}

Result<void> figP13(Ctx& ctx) {
    PlotSpec a = sweepPlot(
        ctx.res, "Iterations vs problem size (HS-7)", "iterations to 1e-10",
        [](const Row& r) { return num(r, "iterations"); }, true, nullptr);
    a.series.erase(
        std::remove_if(a.series.begin(), a.series.end(),
                       [](const PlotSeries& s) { return s.label.rfind("DIRECT", 0) == 0; }),
        a.series.end());
    a.note = "data: results/v2/csv/E2.csv";
    if (auto r = ctx.plot("P13", "P13a_iterations_vs_dof.svg", a, "results/v2/csv/E2.csv"); !r) {
        return r;
    }
    // E3: h (Biot number) and TIM conductivity.
    const auto e3 = [&](const std::string& title, const std::string& xLabel,
                        const std::vector<std::pair<std::string, double>>& systems,
                        const std::string& file) {
        PlotSpec spec;
        spec.title = title;
        spec.xLabel = xLabel;
        spec.yLabel = "iterations to 1e-10";
        spec.logX = spec.logY = true;
        spec.powerTicks = true;
        for (const std::string solver : {"PCG-J", "PCG-IC0", "AMG", "AMG-PCG"}) {
            PlotSeries s = styled(solver);
            for (const auto& [prefix, x] : systems) {
                const Row* r = ctx.res.find(prefix + "-s0.500-" + solver);
                if (r != nullptr && converged(*r)) {
                    s.x.push_back(x);
                    s.y.push_back(num(*r, "iterations"));
                }
            }
            if (!s.x.empty()) {
                spec.series.push_back(s);
            }
        }
        spec.note = "data: results/v2/csv/E3.csv; HS-7 at s = 0.5 mm";
        return ctx.plot("P13", file, spec, "results/v2/csv/E3.csv");
    };
    if (auto r = e3(
            "Iterations vs film coefficient h (HS-7, 286 k DOF)", "h (W/(m²·K))",
            {{"E3-HS7-h5", 5.0}, {"E3-HS7", 25.0}, {"E3-HS7-h100", 100.0}, {"E3-HS7-h500", 500.0}},
            "P13b_iterations_vs_h.svg");
        !r) {
        return r;
    }
    return e3("Iterations vs TIM conductivity (contrast to aluminium 200 W/(m·K))",
              "k_TIM (W/(m·K))",
              {{"E3-HS7-kTim0.5", 0.5},
               {"E3-HS7", 3.0},
               {"E3-HS7-kTim20", 20.0},
               {"E3-HS7-kTim200", 200.0}},
              "P13c_iterations_vs_ktim.svg");
}

Result<void> figP14(Ctx& ctx) {
    BarChartSpec spec;
    const std::string sys = "HS7-s0.630";
    spec.title = "Setup vs solve for 1 and 20 load cases (HS-7, 171 k DOF)";
    spec.yLabel = "time (s)";
    BarStack setup{"setup, once (light)", kInk, {}, true};
    BarStack solve{"solve × load cases", kInk, {}, false};
    std::vector<std::string> colors;
    std::set<std::string> seen;
    for (const int k : {1, 20}) {
        for (const std::string& want : solverOrder()) {
            for (const Row& r : ctx.res.e5) {
                if (str(r, "system") != sys || str(r, "solver") != want) {
                    continue;
                }
                const std::string solver = str(r, "solver");
                if (!seen.insert(solver + std::to_string(k)).second) {
                    continue; // the E2 observer pair appears twice
                }
                spec.categories.push_back(solver + "\nk = " + std::to_string(k));
                setup.values.push_back(num(r, "setup_s"));
                solve.values.push_back(num(r, "solve_s") * k);
                colors.push_back(familyColor(solver));
            }
        }
    }
    if (spec.categories.empty()) {
        return makeError(ErrorCode::InvalidInput, "P14 needs results/v2/csv/E5_load_cases.csv");
    }
    spec.stacks = {setup, solve};
    spec.barColors = colors;
    spec.width = 1200;
    spec.note = "data: results/v2/csv/E5_load_cases.csv (setup_s + k·solve_s, from E2)";
    return ctx.bars("P14", "P14_setup_vs_solve.svg", spec, "results/v2/csv/E5_load_cases.csv");
}

// P15: complexity ladder thumbnails with the winner.
Result<void> figP15(Ctx& ctx) {
    SvgCanvas c(1400, 760, "P15 complexity ladder");
    heading(c, "Complexity ladder at s = 1 mm: which solver is fastest (setup + one solve)",
            "Ratio = time of the slowest finished solver / time of the fastest. Same physics, "
            "increasingly complex objects.");
    const std::vector<std::pair<std::string, fem3d::Design>> designs = {
        {"bar", fem3d::makeV1Bar()},   {"cube", fem3d::makeCube()},
        {"plate", fem3d::makePlate()}, {"fins", fem3d::makeFinnedPlate()},
        {"HS7", fem3d::makeHS7()},     {"pinfin", fem3d::makePinFin()}};
    const std::vector<std::string> names = {"L0a v1 bar",      "L0b cube", "L1 plate",
                                            "L2 finned plate", "L3 HS-7",  "L4 pin fin"};
    for (std::size_t i = 0; i < designs.size(); ++i) {
        const double cx = 30.0 + static_cast<double>(i % 3) * 455.0;
        const double cy = 80.0 + static_cast<double>(i / 3) * 340.0;
        auto s = meshAndSolve(designs[i].second, designs[i].first == "bar" ? 5e-3 : 2e-3, false);
        if (!s) {
            return std::move(s).error();
        }
        const viz::TriangleMesh body = surface(s.value(), byMaterial(s.value()));
        place(c, isoView(840, 440, -58.0, 26.0), {&body}, {}, cx, cy, 0.5);
        c.text(cx + 4, cy + 240, names[i], 14, "start", kInk, true);
        std::string best, worst;
        double tBest = std::numeric_limits<double>::infinity(), tWorst = 0.0, dof = kNaN;
        for (const std::string& solver : solverOrder()) {
            const Row* r = ctx.res.find("E4-" + designs[i].first + "-s1.000-" + solver);
            if (r == nullptr || !converged(*r)) {
                continue;
            }
            dof = num(*r, "dof");
            const double t = totalSeconds(*r);
            if (t < tBest) {
                tBest = t;
                best = solver;
            }
            if (t > tWorst) {
                tWorst = t;
                worst = solver;
            }
        }
        if (!best.empty()) {
            c.rect(cx + 4, cy + 252, 12, 12, familyColor(best));
            c.text(cx + 22, cy + 263, "fastest " + best + " " + fmt(tBest, "%.3g") + " s", 12.5);
            c.text(cx + 4, cy + 283,
                   fmt(dof, "%.0f") + " DOF; slowest " + worst + " (" +
                       fmt(tWorst / tBest, "%.0f") + "×)",
                   12, "start", kMuted);
        }
    }
    return ctx.canvas("P15", "P15_complexity_ladder.svg", c,
                      "Complexity ladder with winner and ratio",
                      "results/v2/csv/E4.csv; thumbnails at s = 2 mm (bar 5 mm)");
}

// P16: decision chart from the measured E5 totals.
Result<void> figP16(Ctx& ctx) {
    // Winner per (system, k) among the solvers that finished.
    struct Point {
        double dof;
        int k;
        std::string winner;
        double margin;
    };
    std::map<std::string, std::map<std::string, std::array<double, 3>>> tot; // system → solver → k
    std::map<std::string, double> dofOf;
    for (const Row& r : ctx.res.e5) {
        const std::string sys = str(r, "system");
        dofOf[sys] = num(r, "dof");
        tot[sys][str(r, "solver")] = {num(r, "total_k1_s"), num(r, "total_k5_s"),
                                      num(r, "total_k20_s")};
    }
    std::vector<Point> pts;
    for (const auto& [sys, bySolver] : tot) {
        for (int ki = 0; ki < 3; ++ki) {
            std::vector<std::pair<double, std::string>> ranked;
            for (const auto& [solver, t] : bySolver) {
                if (std::isfinite(t[static_cast<std::size_t>(ki)]) && solver != "AMG") {
                    ranked.emplace_back(t[static_cast<std::size_t>(ki)], solver);
                }
            }
            std::sort(ranked.begin(), ranked.end());
            if (ranked.size() >= 2) {
                pts.push_back({dofOf[sys], ki == 0 ? 1 : (ki == 1 ? 5 : 20), ranked[0].second,
                               ranked[1].first / ranked[0].first});
            }
        }
    }
    if (pts.empty()) {
        return makeError(ErrorCode::InvalidInput, "P16 needs results/v2/csv/E5_load_cases.csv");
    }
    // 900 units wide so the text prints as large as the line plots (about 7 pt at full width).
    SvgCanvas c(900, 480, "P16 decision chart");
    c.text(20, 32, "Which solver when: measured winners on HS-7 (single thread)", 18, "start", kInk,
           true);
    c.text(20, 54,
           "Each cell: one measured mesh (E2) and number of load cases k (E5: setup + k · solve).",
           13, "start", kMuted);
    c.text(20, 72, "Colour = fastest family; number = its lead over the runner-up.", 13, "start",
           kMuted);
    const double x0 = 92, x1 = 676, yTop = 116, rowH = 92;
    const double lx0 = std::log10(4.5e4), lx1 = std::log10(4.5e6);
    const auto xOf = [&](double dof) {
        return x0 + (std::log10(dof) - lx0) / (lx1 - lx0) * (x1 - x0);
    };
    const std::vector<int> ks = {20, 5, 1};
    for (std::size_t row = 0; row < ks.size(); ++row) {
        const double y = yTop + rowH * static_cast<double>(row);
        c.text(x0 - 14, y + rowH / 2 + 5, "k = " + std::to_string(ks[row]), 15, "end", kInk, true);
        c.line(x0, y + rowH, x1, y + rowH, "#D3D9E1", 1.0);
        for (const Point& p : pts) {
            if (p.k != ks[row]) {
                continue;
            }
            const double x = xOf(p.dof);
            c.rect(x - 31, y + 10, 62, rowH - 20, familyColor(p.winner), "none", 1.0, 5.0);
            c.text(x, y + rowH / 2 - 3, p.winner, 12, "middle", "#FFFFFF", true);
            c.text(x, y + rowH / 2 + 16, fmt(p.margin, "%.2f") + "×", 13, "middle", "#FFFFFF",
                   false, true);
        }
    }
    const double yAxis = yTop + rowH * 3;
    c.line(x0, yAxis, x1, yAxis, kInk, 1.0);
    for (const double d : {5e4, 1e5, 2e5, 5e5, 1e6, 2e6}) {
        c.line(xOf(d), yAxis, xOf(d), yAxis + 6, kInk);
        c.text(xOf(d), yAxis + 22, powerOfTenLabel(d), 13, "middle", kMuted, false, true);
    }
    c.text((x0 + x1) / 2, yAxis + 46, "DOF (HS-7 spacing ladder 1.0 → 0.2 mm)", 14, "middle");
    // Memory wall of the direct solver.
    double wall = kNaN;
    for (const Row* r : ctx.res.where([](const Row& q) {
             return str(q, "experiment") == "E2" && str(q, "solver") == "DIRECT-ND" &&
                    str(q, "setup_status") == "OverBudget";
         })) {
        wall = std::isfinite(wall) ? std::min(wall, num(*r, "dof")) : num(*r, "dof");
    }
    if (std::isfinite(wall)) {
        const double xw = xOf(wall) - 36;
        c.line(xw, yTop - 8, xw, yAxis, kDirect, 1.4, true);
        c.text(xw - 4, yTop - 12, "direct ND over 8 GB from here", 13, "end", kDirect);
    }
    const double lx = 704;
    c.rect(lx, 112, 18, 14, kDirect);
    c.text(lx + 26, 124, "direct (Cholesky)", 13.5);
    c.rect(lx, 138, 18, 14, kPcg);
    c.text(lx + 26, 150, "PCG (Jacobi, IC(0))", 13.5);
    c.rect(lx, 164, 18, 14, kAmg);
    c.text(lx + 26, 176, "AMG-PCG", 13.5);
    c.textBlock(lx, 214,
                "Read: pick the column of\nyour mesh size and the row\nof your number of load\n"
                "cases. Stand-alone AMG is\nleft out: AMG-PCG uses the\nsame hierarchy and is\n"
                "always faster.\n\nAll cores (E6): PCG-J gains\n2.6× and AMG-PCG 1.13×;\n"
                "serial AMG-PCG still wins.",
                13, 1.4, kMuted);
    return ctx.canvas("P16", "P16_decision_chart.svg", c, "Decision chart: which solver when",
                      "results/v2/csv/E5_load_cases.csv, E2.csv, E6.csv");
}

// P17: software architecture (hand-built).
Result<void> figP17(Ctx& ctx) {
    SvgCanvas c(1400, 720, "P17 architecture");
    heading(c, "Software architecture of the V2 solver study",
            "C++17 core with no third-party code; arrows point from a module to what it uses. "
            "The solvers never see the mesh (lint-checked).");
    const auto box = [&](double x, double y, double w, double h, const std::string& title,
                         const std::string& body, const std::string& color) {
        c.rect(x, y, w, h, "#FFFFFF", color, 1.6, 8);
        c.rect(x, y, w, 30, color, "none", 0, 8);
        c.rect(x, y + 20, w, 10, color);
        c.text(x + 12, y + 21, title, 13.5, "start", "#FFFFFF", true);
        c.textBlock(x + 12, y + 50, body, 11.5, 1.4, kInk);
    };
    box(500, 80, 400, 110, "uniforge_cli study",
        "plan · run · batch · collect · figures\none process per "
        "run, Job Object memory cap,\nP-core pinning, resume",
        kInk);
    box(500, 240, 400, 120, "study",
        "RunSpec (E0–E8), Runner (one run: mesh,\nassemble, setup, timed "
        "solves, audit),\nCollect (CSV), Figures (P1–P22)",
        "#59616E");
    box(60, 420, 380, 150, "fem3d",
        "Design (boxes, materials, BCs)\nHexMesh (graded tensor grid)\n"
        "Element (Q1 hex), Assembly → CSR\nPost (energy audit, sampling)",
        "#CC79A7");
    box(510, 420, 380, 150, "solvers",
        "direct/ Ordering (RCM, ND), Cholesky\nkrylov/ PCG, Jacobi, "
        "IC(0), Lanczos κ\namg/ strength, aggregates, P, V-cycle\n"
        "one interface: setup() + solve()",
        kAmg);
    box(960, 420, 380, 150, "viz",
        "Raster3D (z-buffer), Spy, Colormap,\nPngWriter (deflate), "
        "SvgCanvas\nplain arrays in, no mesh types",
        kPcg);
    box(510, 610, 380, 90, "sparse",
        "CsrMatrix, KernelPool (fixed chunks:\nsame bits for any thread "
        "count), SpMV, SpGEMM",
        kDirect);
    box(960, 610, 380, 90, "common (v1)", "Result/Error, AtomicFile, CsvWriter,\nMetaJson, SvgPlot",
        "#7A828E");
    c.arrow(700, 190, 700, 238);
    c.arrow(600, 360, 260, 418);
    c.arrow(700, 360, 700, 418);
    c.arrow(800, 360, 1140, 418);
    c.arrow(440, 545, 508, 640);
    c.arrow(700, 570, 700, 608);
    c.arrow(1150, 570, 1150, 608);
    c.text(330, 395, "uses", 11, "middle", kMuted);
    c.text(60, 620, "fem3d must not use solvers;", 11.5, "start", kMuted);
    c.text(60, 638, "solvers and viz must not use fem3d", 11.5, "start", kMuted);
    c.text(60, 656, "(tests/lint/module_rules.cmake)", 11.5, "start", kMuted);
    return ctx.canvas("P17", "P17_architecture.svg", c, "Software architecture",
                      "src/core/*, build spec §4");
}

// P18: algorithm flow per solver (hand-built).
Result<void> figP18(Ctx& ctx) {
    SvgCanvas c(1400, 800, "P18 algorithm flow");
    heading(c, "How each solver works",
            "Setup runs once per matrix K; solve runs once per load "
            "vector f. Grey notes: the cost that dominates.");
    const auto column = [&](double x, const std::string& title, const std::string& color,
                            const std::vector<std::pair<std::string, std::string>>& steps) {
        c.text(x + 190, 96, title, 15, "middle", color, true);
        double y = 116;
        for (std::size_t i = 0; i < steps.size(); ++i) {
            const bool solve = steps[i].first.rfind("solve:", 0) == 0;
            const std::string t = solve ? steps[i].first.substr(6) : steps[i].first;
            c.rect(x, y, 380, 70, solve ? color : "#FFFFFF", color, 1.5, 8);
            c.text(x + 14, y + 26, t, 13, "start", solve ? "#FFFFFF" : kInk, true);
            c.textBlock(x + 14, y + 46, steps[i].second, 11.2, 1.3, solve ? "#FFFFFF" : kMuted);
            if (i + 1 < steps.size()) {
                c.arrow(x + 190, y + 70, x + 190, y + 92, color);
            }
            y += 94;
        }
    };
    column(30, "A  Sparse direct (Cholesky)", kDirect,
           {{"1 Ordering", "RCM (bandwidth) or nested dissection\n(separators): P·K·Pᵀ"},
            {"2 Symbolic analysis",
             "elimination tree, exact column counts\n→ memory known before allocating"},
            {"3 Budget check", "factor > 8 GB → OverBudget, stop"},
            {"4 Numeric factorisation",
             "up-looking: row k of L by a sparse\ntriangular solve; cost ∝ Σ count²"},
            {"solve:5 Two triangular solves",
             "L·y = P·f, Lᵀ·z = y; cost ∝ nnz(L),\nindependent of κ(K)"},
            {"solve:6 Checks",
             "pivot ratio and residual: a singular K\nis a Breakdown, not an answer"}});
    column(510, "B  Preconditioned CG (Jacobi, IC(0))", kPcg,
           {{"1 Preconditioner M", "Jacobi: M = diag(K)\nIC(0): L̃L̃ᵀ ≈ K on K's own pattern"},
            {"2 Shift ladder (IC(0))", "pivot ≤ 0 → retry with K + α·diag(K),\nα = 1e-3, 1e-2, …"},
            {"solve:3 CG iteration", "q = K·p (SpMV), α = rᵀz / pᵀq,\nx += αp, r −= αq"},
            {"solve:4 Precondition", "z = M⁻¹r (diagonal scale or two\ntriangular solves)"},
            {"solve:5 Stop test", "‖r‖/‖f‖ ≤ 1e-10; iterations grow\nwith √κ(M⁻¹K)"},
            {"solve:6 Lanczos κ estimate", "from the CG coefficients,\nat no extra SpMV"}});
    column(
        990, "C  Smoothed-aggregation AMG", kAmg,
        {{"1 Strength of connection", "|a_ij| ≥ θ·√(a_ii a_jj), θ halved per level"},
         {"2 Aggregation", "greedy: roots with free neighbours,\nthen joins, then leftovers"},
         {"3 Prolongator P", "piecewise constant, then smoothed:\nP = (I − ω D⁻¹A_F) P₀"},
         {"4 Galerkin hierarchy", "A_c = Pᵀ A P (SpGEMM) down to\n≤ 500 rows, then a direct solve"},
         {"solve:5 V-cycle",
          "symmetric Gauss–Seidel, restrict,\ncoarse correction, prolong, smooth"},
         {"solve:6 Stand-alone or in CG",
          "AMG: repeat V-cycles\nAMG-PCG: one V-cycle as M⁻¹ in CG"}});
    return ctx.canvas("P18", "P18_algorithm_flow.svg", c, "Algorithm flow per solver",
                      "src/core/solvers/{direct,krylov,amg}, build spec §5");
}

// P19: error vs time (time-to-accuracy).
Result<void> figP19(Ctx& ctx) {
    const std::string sys = "E2-HS7-s0.397-";
    PlotSpec spec;
    spec.title = "Error against the reference vs time (HS-7, 568 k DOF)";
    spec.xLabel = "time since the start of setup (s)";
    spec.yLabel = "max |T − T_ref| (K)";
    spec.logX = spec.logY = true;
    spec.powerTicks = true;
    double yLo = std::numeric_limits<double>::infinity(), yHi = 0.0;
    for (const std::string solver : {"PCG-J", "PCG-IC0", "AMG", "AMG-PCG"}) {
        const Row* r = ctx.res.find(sys + solver);
        const auto h = history(ctx.res, sys + solver, "time_s", "error_K");
        if (r == nullptr || h.empty()) {
            continue;
        }
        const double setup = num(*r, "setup_s");
        PlotSeries s = styled(solver);
        s.marker = PlotMarker::None;
        for (const auto& [t, e] : h) {
            if (t + setup > 0.0) {
                s.x.push_back(t + setup);
                s.y.push_back(e);
                yLo = std::min(yLo, e);
                yHi = std::max(yHi, e);
            }
        }
        spec.series.push_back(s);
    }
    const Row* d = ctx.res.find(sys + "DIRECT-ND");
    if (d != nullptr && converged(*d) && yHi > 0.0) {
        PlotSeries s = styled("DIRECT-ND");
        s.label = "DIRECT-ND (answer at the end)";
        s.x = {totalSeconds(*d), totalSeconds(*d)};
        s.y = {yLo, yHi};
        s.marker = PlotMarker::None;
        spec.series.push_back(s);
    }
    if (spec.series.empty()) {
        return makeError(ErrorCode::InvalidInput, "P19 needs results/v2/history/" + sys + "*.csv");
    }
    spec.annotations = {};
    spec.note = "data: results/v2/history/" + sys + "<solver>.csv; reference = DIRECT-ND solution";
    return ctx.plot("P19", "P19_error_vs_time.svg", spec,
                    "E2-HS7-s0.397-*; E2_time_to_accuracy.csv");
}

// P20: iterations vs √κ_est.
Result<void> figP20(Ctx& ctx) {
    PlotSpec spec;
    spec.title = "Iterations vs √κ estimate (E2 sizes and E3 conditioning sweep)";
    spec.xLabel = "√κ_est of the preconditioned operator";
    spec.yLabel = "iterations to 1e-10";
    spec.logX = spec.logY = true;
    spec.powerTicks = true;
    double lo = std::numeric_limits<double>::infinity(), hi = 0.0;
    for (const std::string solver : {"PCG-J", "PCG-IC0", "AMG-PCG"}) {
        PlotSeries s = styled(solver);
        s.line = false;
        for (const Row* r : ctx.res.where([&](const Row& q) {
                 return (str(q, "experiment") == "E2" || str(q, "experiment") == "E3") &&
                        str(q, "solver") == solver && converged(q) && num(q, "kappa_est") > 1.0;
             })) {
            s.x.push_back(std::sqrt(num(*r, "kappa_est")));
            s.y.push_back(num(*r, "iterations"));
            lo = std::min(lo, s.x.back());
            hi = std::max(hi, s.x.back());
        }
        if (!s.x.empty()) {
            spec.annotations.push_back({s.x.back(), s.y.back(),
                                        "slope " + fmt(logSlope(s.x, s.y), "%.2f"), s.color, 8.0,
                                        4.0});
            spec.series.push_back(s);
        }
    }
    if (spec.series.empty()) {
        return makeError(ErrorCode::InvalidInput, "P20 needs κ estimates in E2/E3");
    }
    spec.series.push_back(reference("∝ √κ (CG bound shape)", {lo, hi}, {lo * 0.5, hi * 0.5}));
    spec.note = "data: results/v2/csv/E2.csv, E3.csv (kappa_est from the CG-Lanczos tridiagonal)";
    return ctx.plot("P20", "P20_iterations_vs_sqrt_kappa.svg", spec,
                    "results/v2/csv/E2.csv, E3.csv");
}

// P21: failure modes.
Result<void> figP21(Ctx& ctx) {
    // Rows first (the canvas height follows the content, so there is no empty area).
    struct Line {
        std::string solver, setup, solve;
        std::vector<std::string> detail;
    };
    std::vector<Line> lines;
    for (const std::string& solver : solverOrder()) {
        const Row* r = ctx.res.find("E8-HS7-singular-s2.000-" + solver + "-noobs");
        if (r == nullptr) {
            continue;
        }
        std::string detail = str(*r, "detail");
        // Result rows are ASCII (RunRecord::toCsv turned the superscript T into '?').
        for (std::size_t at = detail.find("p?Kp"); at != std::string::npos;
             at = detail.find("p?Kp")) {
            detail.replace(at, 4, "pᵀKp");
        }
        // Wrap at word boundaries, about 60 characters per line, at most four lines.
        std::vector<std::string> wrapped;
        std::string cur;
        std::size_t pos = 0;
        while (pos < detail.size()) {
            std::size_t sp = detail.find(' ', pos);
            const std::string word =
                detail.substr(pos, sp == std::string::npos ? std::string::npos : sp - pos);
            if (!cur.empty() && cur.size() + 1 + word.size() > 60) {
                wrapped.push_back(cur);
                cur.clear();
            }
            cur += (cur.empty() ? "" : " ") + word;
            pos = sp == std::string::npos ? detail.size() : sp + 1;
        }
        if (!cur.empty()) {
            wrapped.push_back(cur);
        }
        if (wrapped.size() > 4) {
            wrapped.resize(4);
            wrapped.back() += " …";
        }
        lines.push_back({solver, str(*r, "setup_status"), str(*r, "solve_status"), wrapped});
    }
    std::map<std::string, int> counts;
    for (const Row& r : ctx.res.runs) {
        const std::string st = str(r, "solve_status");
        counts[st == "not run" ? "setup " + str(r, "setup_status")
                               : (st.empty() ? "reference/check" : st)]++;
    }
    double rowsH = 0.0;
    for (const Line& l : lines) {
        rowsH += 12.0 + 19.0 * static_cast<double>(std::max<std::size_t>(1, l.detail.size()));
    }
    const double height = 110.0 + rowsH + 50.0 + 26.0 * static_cast<double>(counts.size()) + 40.0;
    SvgCanvas c(1000, static_cast<int>(height), "P21 failure modes");
    heading(c, "Failure modes: what each solver reports instead of a wrong answer",
            "Top: E8, HS-7 with every face insulated (K singular). Bottom: all " +
                std::to_string(ctx.res.runs.size()) + " official runs by outcome.");
    double y = 96;
    c.text(24, y, "solver", 14, "start", kMuted, true);
    c.text(170, y, "setup", 14, "start", kMuted, true);
    c.text(290, y, "solve", 14, "start", kMuted, true);
    c.text(410, y, "what it reported", 14, "start", kMuted, true);
    y += 8;
    for (const Line& l : lines) {
        y += 22;
        c.rect(18, y - 16, 2, 19.0 * static_cast<double>(l.detail.size()), familyColor(l.solver));
        c.text(24, y, l.solver, 15, "start", familyColor(l.solver), true);
        c.text(170, y, l.setup, 14, "start", kInk, false, true);
        c.text(290, y, l.solve, 14, "start", kInk, false, true);
        for (std::size_t k = 0; k < l.detail.size(); ++k) {
            c.text(410, y + 19.0 * static_cast<double>(k), l.detail[k], 13.5, "start", kMuted);
        }
        y += 19.0 * static_cast<double>(std::max<std::size_t>(1, l.detail.size())) - 10.0;
    }
    y += 40;
    c.line(18, y - 24, 982, y - 24, "#D3D9E1", 1.0);
    c.text(24, y, "outcome (all runs)", 14, "start", kMuted, true);
    c.text(360, y, "runs", 14, "end", kMuted, true);
    for (const auto& [k, n] : counts) {
        y += 26;
        c.text(24, y, k, 15);
        c.text(360, y, std::to_string(n), 15, "end", kInk, false, true);
    }
    c.textBlock(410, y - 26.0 * static_cast<double>(counts.size()) + 6.0,
                "No run crashed, hit a time cap or was killed by the memory cap.\n"
                "Breakdowns: the singular E8 cases only.\n"
                "Over budget: the direct solvers on the largest meshes, refused\n"
                "by the symbolic size check before any memory was allocated.",
                14, 1.45, kMuted);
    return ctx.canvas("P21", "P21_failure_modes.svg", c, "Failure-mode table and E8 outcomes",
                      "results/v2/csv/E8.csv, all_runs.csv");
}

// P22: E0 bar vs cube.
Result<void> figP22(Ctx& ctx) {
    BarChartSpec spec;
    spec.title = "Same physics, two simple objects: v1 bar (10 k DOF) vs cube (16 k DOF)";
    spec.yLabel = "setup + solve (ms, log scale)";
    spec.logY = true;
    BarStack setup{"setup (light)", kInk, {}, true};
    BarStack solve{"solve", kInk, {}, false};
    for (const auto& [obj, sys] : std::vector<std::pair<std::string, std::string>>{
             {"bar", "E0-bar-s2.500-"}, {"cube", "E0-cube-s1.934-"}}) {
        for (const std::string& solver : solverOrder()) {
            const Row* r = ctx.res.find(sys + solver);
            if (r == nullptr || !converged(*r)) {
                continue;
            }
            // "ND" / "RCM": the colour already says direct, and the full names collide.
            const std::string shortName =
                solver.rfind("DIRECT-", 0) == 0 ? solver.substr(7) : solver;
            spec.categories.push_back(shortName + "\n" + obj);
            setup.values.push_back(num(*r, "setup_s") * 1e3);
            solve.values.push_back(num(*r, "solve_s_median") * 1e3);
            spec.barColors.push_back(familyColor(solver));
        }
    }
    if (spec.categories.empty()) {
        return makeError(ErrorCode::InvalidInput, "P22 needs results/v2/csv/E0.csv");
    }
    spec.stacks = {setup, solve};
    spec.width = 1000;
    spec.height = 460;
    spec.note =
        "data: results/v2/csv/E0.csv; all six give the same peak temperature (188.2 °C, 100.0 °C)";
    return ctx.bars("P22", "P22_bar_vs_cube.svg", spec, "results/v2/csv/E0.csv");
}

// P23: E6 thread scaling of PCG-J and AMG-PCG.
Result<void> figP23(Ctx& ctx) {
    PlotSpec spec;
    spec.title = "All cores: time vs threads (HS-7, 568 k DOF, E6)";
    spec.xLabel = "threads";
    spec.yLabel = "setup + solve (s)";
    spec.logX = spec.logY = true;
    double serialJ = kNaN;
    for (const std::string solver : {"PCG-J", "AMG-PCG"}) {
        auto rows = ctx.res.where([&](const Row& r) {
            return str(r, "experiment") == "E6" && str(r, "solver") == solver && converged(r);
        });
        std::sort(rows.begin(), rows.end(), [](const Row* a, const Row* b) {
            return num(*a, "threads") < num(*b, "threads");
        });
        PlotSeries ser = styled(solver);
        for (const Row* r : rows) {
            ser.x.push_back(num(*r, "threads"));
            ser.y.push_back(num(*r, "total_s_median"));
        }
        if (ser.x.empty()) {
            continue;
        }
        if (solver == "PCG-J") {
            serialJ = ser.y.front();
        }
        spec.annotations.push_back({ser.x.back(), ser.y.back(),
                                    "speed-up " + fmt(ser.y.front() / ser.y.back(), "%.2f") +
                                        "× at " + fmt(ser.x.back(), "%.0f") + " threads",
                                    ser.color, 8.0, 4.0});
        spec.series.push_back(ser);
    }
    if (spec.series.empty()) {
        return makeError(ErrorCode::InvalidInput, "P23 needs the E6 rows of all_runs.csv");
    }
    if (std::isfinite(serialJ)) {
        spec.series.push_back(
            reference("ideal speed-up from serial PCG-J", {1.0, 20.0}, {serialJ, serialJ / 20.0}));
    }
    spec.height = 430;
    spec.note = "data: results/v2/csv/E6.csv (one process per run, medians)";
    return ctx.plot("P23", "P23_thread_scaling.svg", spec, "results/v2/csv/E6.csv");
}

// P24: the AMG strength-threshold fix, before (θ₀ = 0.08) and after (0.02), on the E2 sweep.
Result<void> figP24(Ctx& ctx) {
    const auto file = ctx.opt.results.parent_path() / "v2_superseded" / "amg_theta008" /
                      "compare_theta008_vs_002.csv";
    auto rows = readCsv(file);
    if (!rows) {
        return std::move(rows).error();
    }
    PlotSpec spec;
    spec.title = "AMG strength threshold: before (θ₀ = 0.08) and after (θ₀ = 0.02), HS-7";
    spec.xLabel = "DOF";
    spec.yLabel = "iterations / cycles to 1e-10";
    spec.logX = spec.logY = true;
    spec.powerTicks = true;
    struct Pick {
        std::string solver, theta, label;
        bool after;
    };
    const std::vector<Pick> picks = {{"AMG-PCG", "008", "AMG-PCG, θ₀ = 0.08 (before)", false},
                                     {"AMG", "008", "AMG, θ₀ = 0.08 (before)", false},
                                     {"AMG-PCG", "002", "AMG-PCG, θ₀ = 0.02 (after)", true},
                                     {"AMG", "002", "AMG, θ₀ = 0.02 (after)", true}};
    PlotSeries stalled;
    stalled.label = "stopped by the stagnation check";
    stalled.color = kGrey;
    stalled.line = false;
    stalled.marker = PlotMarker::Cross;
    for (const Pick& pk : picks) {
        std::vector<std::pair<double, double>> pts;
        for (const Row& r : rows.value()) {
            const std::string id = str(r, "id");
            const std::string suffix = "-" + pk.solver;
            if (id.rfind("E2-HS7-s", 0) != 0 || id.size() < suffix.size() ||
                id.compare(id.size() - suffix.size(), suffix.size(), suffix) != 0) {
                continue;
            }
            const double it = num(r, "iterations_" + pk.theta);
            if (str(r, "status_" + pk.theta) == "Stagnated") {
                stalled.x.push_back(num(r, "dof"));
                stalled.y.push_back(it);
                continue;
            }
            pts.emplace_back(num(r, "dof"), it);
        }
        std::sort(pts.begin(), pts.end());
        PlotSeries ser = styled(pk.solver);
        ser.label = pk.label;
        if (!pk.after) {
            ser.color = kGrey;
        }
        for (const auto& [x, y] : pts) {
            ser.x.push_back(x);
            ser.y.push_back(y);
        }
        if (!ser.x.empty()) {
            spec.series.push_back(ser);
        }
    }
    if (!stalled.x.empty()) {
        spec.series.push_back(stalled);
    }
    if (spec.series.empty()) {
        return makeError(ErrorCode::InvalidInput, "P24: no E2 rows in " + file.u8string());
    }
    spec.height = 430;
    spec.note = "data: results/v2_superseded/amg_theta008/compare_theta008_vs_002.csv";
    return ctx.plot("P24", "P24_amg_threshold_fix.svg", spec,
                    "results/v2_superseded/amg_theta008/compare_theta008_vs_002.csv");
}

bool wanted(const FigureOptions& o, const std::string& id) {
    return o.only.empty() || std::find(o.only.begin(), o.only.end(), id) != o.only.end();
}

} // namespace

Result<std::vector<FigureEntry>> makeFigures(const FigureOptions& options) {
    std::error_code ec;
    std::filesystem::create_directories(options.out, ec);
    if (ec) {
        return makeError(ErrorCode::IoError, "Cannot create " + options.out.u8string());
    }
    Results res;
    res.root = options.results;
    auto runs = readCsv(options.results / "csv" / "all_runs.csv");
    if (!runs) {
        return std::move(runs).error();
    }
    res.runs = std::move(runs).value();
    if (auto e5 = readCsv(options.results / "csv" / "E5_load_cases.csv"); e5) {
        res.e5 = std::move(e5).value();
    }
    std::vector<FigureEntry> entries;
    Ctx ctx{options, res, entries};

    const auto run = [&](const std::string& id,
                         const std::function<Result<void>()>& f) -> Result<void> {
        if (!wanted(options, id)) {
            return {};
        }
        auto r = f();
        if (!r) {
            return makeError(r.error().code, id + ": " + r.error().message, r.error().hint);
        }
        return {};
    };
    using Fn = std::function<Result<void>()>;
    const std::vector<std::pair<std::string, Fn>> simple = {
        {"P1", [&] { return figP1(ctx); }},   {"P2", [&] { return figP2(ctx); }},
        {"P3", [&] { return figP3(ctx); }},   {"P6", [&] { return figP6(ctx); }},
        {"P7", [&] { return figP7(ctx); }},   {"P8", [&] { return figP8(ctx); }},
        {"P9", [&] { return figP9(ctx); }},   {"P10", [&] { return figP10(ctx); }},
        {"P11", [&] { return figP11(ctx); }}, {"P12", [&] { return figP12(ctx); }},
        {"P13", [&] { return figP13(ctx); }}, {"P14", [&] { return figP14(ctx); }},
        {"P15", [&] { return figP15(ctx); }}, {"P16", [&] { return figP16(ctx); }},
        {"P17", [&] { return figP17(ctx); }}, {"P18", [&] { return figP18(ctx); }},
        {"P19", [&] { return figP19(ctx); }}, {"P20", [&] { return figP20(ctx); }},
        {"P21", [&] { return figP21(ctx); }}, {"P22", [&] { return figP22(ctx); }},
        {"P23", [&] { return figP23(ctx); }}, {"P24", [&] { return figP24(ctx); }}};
    for (const auto& [id, f] : simple) {
        if (auto r = run(id, f); !r) {
            return std::move(r).error();
        }
    }
    // P4 and P5 share one HS-7 solve at 0.5 mm.
    if (wanted(options, "P4") || wanted(options, "P5")) {
        auto hs = meshAndSolve(fem3d::makeHS7(), 5e-4, true);
        if (!hs) {
            return makeError(hs.error().code, "P4/P5: " + hs.error().message);
        }
        if (auto r = run("P4", [&] { return figP4(ctx, hs.value()); }); !r) {
            return std::move(r).error();
        }
        if (auto r = run("P5", [&] { return figP5(ctx, hs.value()); }); !r) {
            return std::move(r).error();
        }
    }
    std::sort(entries.begin(), entries.end(), [](const FigureEntry& a, const FigureEntry& b) {
        const auto key = [](const std::string& id) { return std::stoi(id.substr(1)); };
        return key(a.id) != key(b.id) ? key(a.id) < key(b.id) : a.file < b.file;
    });
    std::string md = "# V2 report figures\n\nWritten by `uniforge_cli study figures` from `" +
                     options.results.generic_u8string() +
                     "` (build spec §12.1). Every figure maps to the data it is drawn from. 3D "
                     "renders and spy rasters are PNG files next to their SVG "
                     "(`<figure>-<k>.png`).\n\n"
                     "| Figure | File | What it shows | Drawn from |\n|---|---|---|---|\n";
    for (const auto& e : entries) {
        md += "| " + e.id + " | [" + e.file + "](" + e.file + ") | " + e.title + " | " + e.sources +
              " |\n";
    }
    if (options.only.empty()) {
        if (auto w = writeFileAtomic(options.out / "figures.md", md); !w) {
            return std::move(w).error();
        }
    }
    return entries;
}

} // namespace uniforge::study
