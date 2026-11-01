#include "core/common/Rng.hpp"

#include <cassert>
#include <cmath>

namespace uniforge {

double Rng::uniform01() {
    // 2^-53: every value k · 2^-53 for k < 2^53 is an exact double, so no rounding happens here.
    constexpr double kScale = 1.0 / 9007199254740992.0;
    return static_cast<double>(next() >> 11) * kScale;
}

double Rng::uniform(double a, double b) {
    assert(a < b && std::isfinite(a) && std::isfinite(b));
    const double x = a + (b - a) * uniform01();
    // a + (b − a)·u can round up to b when u is just below 1; keep the interval half-open.
    return x < b ? x : std::nextafter(b, a);
}

} // namespace uniforge
