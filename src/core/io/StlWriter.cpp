#include "core/io/StlWriter.hpp"

#include <cmath>

namespace uniforge::io {

namespace {

void appendVec(std::string& out, const Vec3d& p) {
    out += formatShortest(p.x);
    out += ' ';
    out += formatShortest(p.y);
    out += ' ';
    out += formatShortest(p.z);
    out += '\n';
}

} // namespace

void appendStlBegin(std::string& out, std::string_view name) {
    out += "solid ";
    out += name;
    out += '\n';
}

void appendStlFacet(std::string& out, const std::array<Vec3d, 3>& v) {
    const Vec3d a{v[1].x - v[0].x, v[1].y - v[0].y, v[1].z - v[0].z};
    const Vec3d b{v[2].x - v[0].x, v[2].y - v[0].y, v[2].z - v[0].z};
    Vec3d n{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    const double len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    n = len > 0.0 ? Vec3d{n.x / len, n.y / len, n.z / len} : Vec3d{};

    out += "  facet normal ";
    appendVec(out, n);
    out += "    outer loop\n";
    for (const Vec3d& p : v) {
        out += "      vertex ";
        appendVec(out, p);
    }
    out += "    endloop\n  endfacet\n";
}

void appendStlEnd(std::string& out, std::string_view name) {
    out += "endsolid ";
    out += name;
    out += '\n';
}

} // namespace uniforge::io
