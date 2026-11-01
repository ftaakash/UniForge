#include "core/thermal/Analytical.hpp"

#include <cmath>

namespace uniforge::thermal {

double exactTemperature(const RodProblem& p, double x) noexcept {
    return p.T0 + (p.TL - p.T0) * x / p.L + p.q / (2.0 * p.k) * x * (p.L - x);
}

TemperaturePeak exactPeak(const RodProblem& p) noexcept {
    TemperaturePeak best = p.T0 >= p.TL ? TemperaturePeak{0.0, p.T0} : TemperaturePeak{p.L, p.TL};
    if (p.q > 0.0) {
        const double xs = p.L / 2.0 + p.k * (p.TL - p.T0) / (p.q * p.L);
        if (xs > 0.0 && xs < p.L) {
            const double Ts = exactTemperature(p, xs);
            if (Ts > best.T) {
                best = {xs, Ts};
            }
        }
    }
    return best;
}

double expectedMidpointError(const RodProblem& p, double h) noexcept {
    return std::abs(p.q) * h * h / (8.0 * p.k);
}

} // namespace uniforge::thermal
