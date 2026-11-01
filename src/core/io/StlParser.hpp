#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <string>
#include <vector>

#include "core/common/CancelToken.hpp"
#include "core/common/Progress.hpp"
#include "core/common/Result.hpp"
#include "core/io/ImportLimits.hpp"
#include "core/io/ImportTypes.hpp"

namespace uniforge::io {

// Triangle mesh from an ASCII STL file (spec §5.3). Coordinates are in the file's length unit;
// STL has no unit field, and UniForge's samples and views treat it as metres.
struct StlMesh {
    std::string name;                      // from the first `solid` line (may be empty)
    std::vector<std::array<Vec3d, 3>> tri; // vertices in file order
    std::vector<Vec3d> normal;             // unit normal per triangle; (0,0,0) if degenerate
    Vec3d bbMin{};
    Vec3d bbMax{};
    std::size_t degenerate = 0;        // zero-area triangles (kept, reported as a warning)
    std::size_t recomputedNormals = 0; // zero normals in the file, recomputed from the vertices
    ImportStats stats;
};

// Parses ASCII STL with a line-by-line state machine (spec §7.2):
//     solid name
//       facet normal nx ny nz
//         outer loop
//           vertex x y z   (exactly three)
//         endloop
//       endfacet
//       ...
//     endsolid name
// Keywords are matched case-insensitively; blank lines are ignored; several solids in one file
// are merged. Never throws; failures are typed errors with a 1-based line where one applies:
//   EmptyFile          0 bytes / only blank lines, or a solid without facets (different messages)
//   UnsupportedFormat  binary STL, or the first line is not `solid` ("Export as ASCII STL")
//   ParseError         unknown keyword, keyword out of order, wrong vertex or number count,
//                      text where a number belongs, a line over maxLineBytes, or the file ends
//                      inside a facet (truncated)
//   NonFinite          nan / inf, or a number outside the double range
//   LimitExceeded      more than limits.maxFacets facets or limits.maxBytes bytes
//   Cancelled, IoError
// Zero normals are recomputed from the vertex order (right-hand rule) and other normals are
// normalised; degenerate triangles are counted. sizeHint (bytes, 0 = unknown) drives progress
// and the binary-STL size check.
[[nodiscard]] Result<StlMesh> parseAsciiStl(std::istream& in, std::uintmax_t sizeHint,
                                            const ImportLimits& limits = {},
                                            ProgressFn progress = {},
                                            const CancelToken* cancel = nullptr);

// Opens `path` (binary mode, Unicode-safe) and calls parseAsciiStl with its size. A missing or
// unreadable file is IoError naming the file. An unnamed solid takes the file name's stem.
[[nodiscard]] Result<StlMesh> parseAsciiStlFile(const std::filesystem::path& path,
                                                const ImportLimits& limits = {},
                                                ProgressFn progress = {},
                                                const CancelToken* cancel = nullptr);

} // namespace uniforge::io
