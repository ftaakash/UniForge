#pragma once

#include <array>
#include <string>
#include <string_view>

#include "core/common/CsvWriter.hpp" // formatShortest
#include "core/io/ImportTypes.hpp"

namespace uniforge::io {

// ASCII STL text output for the sample generator, the import benchmark and tests. Numbers use
// formatShortest (the shortest text that reads back to the same double), so files are
// small, readable and byte-identical on every compiler; -0 is written as 0. Lines end in LF.
//
//     std::string stl;
//     appendStlBegin(stl, "rod");
//     appendStlFacet(stl, {Vec3d{0, 0, 0}, Vec3d{1, 0, 0}, Vec3d{0, 1, 0}});
//     appendStlEnd(stl, "rod");

void appendStlBegin(std::string& out, std::string_view name);
// The facet normal is computed from the vertex order (right-hand rule); (0,0,0) if degenerate.
void appendStlFacet(std::string& out, const std::array<Vec3d, 3>& v);
void appendStlEnd(std::string& out, std::string_view name);

} // namespace uniforge::io
