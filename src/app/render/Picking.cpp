#include "app/render/Picking.hpp"

namespace uniforge::render {

std::optional<PickHit> pickNearest(const std::vector<float>& xyz, const Mat4& viewProj,
                                   double widthPx, double heightPx, double cursorX, double cursorY,
                                   double radiusPx) {
    constexpr double kTiePx = 0.5;
    std::optional<PickHit> best;
    const std::size_t n = xyz.size() / 3;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3 p{xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]};
        const ScreenPoint s = projectToScreen(viewProj, p, widthPx, heightPx);
        if (!s.visible) {
            continue;
        }
        const double dx = s.x - cursorX;
        const double dy = s.y - cursorY;
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d > radiusPx) {
            continue;
        }
        const bool better = !best || d < best->distancePx - kTiePx ||
                            (d <= best->distancePx + kTiePx && s.depth < best->depth);
        if (better) {
            best = PickHit{i, d, s.depth};
        }
    }
    return best;
}

} // namespace uniforge::render
