#include "core/ml/GradientCheck.hpp"

#include <cmath>

namespace uniforge::ml {

namespace {

void setGrad(Grads& g, int i, double value) {
    switch (i) {
    case 0:
        g.dw = value;
        break;
    case 1:
        g.db = value;
        break;
    case 2:
        g.dv = value;
        break;
    default:
        g.dc = value;
        break;
    }
}

} // namespace

GradCheck centralDiff(const Params& p, double x, double y, double eps, double tolerance) {
    GradCheck check;
    check.eps = eps;
    check.tolerance = tolerance;
    check.analytic = backward(p, x, y);
    check.pass = true;
    for (int i = 0; i < kParamCount; ++i) {
        Params plus = p;
        Params minus = p;
        set(plus, i, get(p, i) + eps);
        set(minus, i, get(p, i) - eps);
        const double numeric = (loss(plus, x, y) - loss(minus, x, y)) / (2.0 * eps);
        const double diff = std::abs(get(check.analytic, i) - numeric);
        setGrad(check.numeric, i, numeric);
        setGrad(check.absDiff, i, diff);
        // Written so that a NaN difference fails.
        check.passEach[static_cast<std::size_t>(i)] = diff <= tolerance;
        check.pass = check.pass && diff <= tolerance;
    }
    return check;
}

} // namespace uniforge::ml
