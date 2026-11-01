#include "core/ml/TinyNet.hpp"

#include <cassert>
#include <cmath>
#include <string>

namespace uniforge::ml {

double get(const Params& p, int i) {
    assert(i >= 0 && i < kParamCount);
    switch (i) {
    case 0:
        return p.w;
    case 1:
        return p.b;
    case 2:
        return p.v;
    default:
        return p.c;
    }
}

double get(const Grads& g, int i) {
    assert(i >= 0 && i < kParamCount);
    switch (i) {
    case 0:
        return g.dw;
    case 1:
        return g.db;
    case 2:
        return g.dv;
    default:
        return g.dc;
    }
}

void set(Params& p, int i, double value) {
    assert(i >= 0 && i < kParamCount);
    switch (i) {
    case 0:
        p.w = value;
        break;
    case 1:
        p.b = value;
        break;
    case 2:
        p.v = value;
        break;
    default:
        p.c = value;
        break;
    }
}

Forward forward(const Params& p, double x, double y) {
    Forward f;
    f.z = p.w * x + p.b;
    f.h = std::tanh(f.z);
    f.yhat = p.v * f.h + p.c;
    f.err = f.yhat - y;
    f.loss = 0.5 * f.err * f.err;
    return f;
}

double loss(const Params& p, double x, double y) {
    return forward(p, x, y).loss;
}

Grads backward(const Params& p, double x, double y) {
    const Forward f = forward(p, x, y);
    Grads g;
    g.dc = f.err;                               // ∂J/∂ŷ · ∂ŷ/∂c = e · 1
    g.dv = f.err * f.h;                         // e · h
    g.db = f.err * p.v * (1.0 - f.h * f.h);     // e · v · tanh'(z), tanh' = 1 − h²
    g.dw = g.db * x;                            // ∂z/∂w = x, ∂z/∂b = 1
    return g;
}

Params sgdStep(const Params& p, const Grads& g, double lr) {
    return Params{p.w - lr * g.dw, p.b - lr * g.db, p.v - lr * g.dv, p.c - lr * g.dc};
}

Result<void> BackpropProblem::validate() const {
    struct Value {
        const char* name;
        double value;
    };
    const Value values[] = {{"Input x", x},       {"Target y", y},       {"Weight w", init.w},
                            {"Bias b", init.b},   {"Weight v", init.v},  {"Bias c", init.c},
                            {"Learning rate lr", lr}};
    for (const Value& v : values) {
        if (!std::isfinite(v.value)) {
            return makeError(ErrorCode::NonFinite,
                             std::string(v.name) + " is not a finite number.",
                             "Enter a finite value.");
        }
    }
    if (!(lr > 0.0)) {
        return makeError(ErrorCode::InvalidInput, "Learning rate lr must be greater than 0.",
                         "Enter a positive learning rate, e.g. 0.1.");
    }
    if (steps < 1) {
        return makeError(ErrorCode::InvalidInput,
                         "Training steps = " + std::to_string(steps) + " must be at least 1.",
                         "Enter an integer number of steps >= 1, e.g. 100.");
    }
    if (steps > kMaxSteps) {
        return makeError(ErrorCode::LimitExceeded,
                         "Training steps = " + std::to_string(steps) + " exceeds the limit of " +
                             std::to_string(kMaxSteps) + ".",
                         "Use at most 1,000,000 steps.");
    }
    return {};
}

} // namespace uniforge::ml
