#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace uniforge::io {

// Double-precision point or vector in metres (spec §5.3). Importers keep doubles; the renderer
// recentres on the bounding-box centre before converting to float (Phase 4).
struct Vec3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// Facts about one import, shown in the success summary and logged (spec §8.5, §9).
struct ImportStats {
    std::uintmax_t bytes = 0;          // bytes read from the stream, BOM included
    std::size_t lines = 0;             // physical lines read
    double parseSeconds = 0.0;         // wall time inside the parser
    std::vector<std::string> warnings; // complete sentences, e.g. "3 degenerate triangles ..."
};

// Axis-aligned bounding box accumulator. Starts empty (min > max) until the first point.
class BoundsAccumulator {
public:
    void add(const Vec3d& p) noexcept {
        if (empty_) {
            min_ = p;
            max_ = p;
            empty_ = false;
            return;
        }
        min_.x = p.x < min_.x ? p.x : min_.x;
        min_.y = p.y < min_.y ? p.y : min_.y;
        min_.z = p.z < min_.z ? p.z : min_.z;
        max_.x = p.x > max_.x ? p.x : max_.x;
        max_.y = p.y > max_.y ? p.y : max_.y;
        max_.z = p.z > max_.z ? p.z : max_.z;
    }
    [[nodiscard]] bool empty() const noexcept { return empty_; }
    [[nodiscard]] const Vec3d& min() const noexcept { return min_; }
    [[nodiscard]] const Vec3d& max() const noexcept { return max_; }

private:
    Vec3d min_{};
    Vec3d max_{};
    bool empty_ = true;
};

} // namespace uniforge::io
