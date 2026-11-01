#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "app/render/RenderMath.hpp"

namespace uniforge::render {

struct PickHit {
    std::size_t index = 0;   // into the point array
    double distancePx = 0.0; // from the cursor
    double depth = 0.0;      // NDC depth, smaller = nearer
};

inline constexpr double kPickRadiusPx = 6.0;

// CPU screen-space picking (spec §7.2): projects every point with `viewProj`, keeps the ones
// within `radiusPx` of the cursor and returns the nearest to the cursor; on a tie (within
// 0.5 px) the one nearer the camera wins. Points are float xyz triples in scene coordinates.
// O(n): 1 M points take a few milliseconds, and it only runs on a click.
[[nodiscard]] std::optional<PickHit> pickNearest(const std::vector<float>& xyz,
                                                 const Mat4& viewProj, double widthPx,
                                                 double heightPx, double cursorX, double cursorY,
                                                 double radiusPx = kPickRadiusPx);

} // namespace uniforge::render
