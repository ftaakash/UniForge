#pragma once

#include <array>

#include "core/common/Result.hpp"

namespace uniforge::ml {

// One-neuron network of Task F, trained on a single sample (x, y):
//
//     z = w·x + b,   h = tanh(z),   ŷ = v·h + c,   e = ŷ − y,   J = ½·e²
//
// Gradients are derived by hand with the chain rule (no autodiff). Every function here is pure
// and deterministic; non-finite inputs propagate as NaN/inf, they never cause UB. Callers that
// take user input check BackpropProblem::validate() first.
struct Params {
    double w = 0.0, b = 0.0, v = 0.0, c = 0.0;
};

struct Grads {
    double dw = 0.0, db = 0.0, dv = 0.0, dc = 0.0;
};

struct Forward {
    double z = 0.0, h = 0.0, yhat = 0.0, err = 0.0, loss = 0.0;
};

// Parameters in the fixed order w, b, v, c (tables, CSV rows, finite-difference loop).
inline constexpr int kParamCount = 4;
inline constexpr std::array<const char*, kParamCount> kParamNames{"w", "b", "v", "c"};

[[nodiscard]] double get(const Params& p, int i);
[[nodiscard]] double get(const Grads& g, int i);
void set(Params& p, int i, double value);

[[nodiscard]] Forward forward(const Params& p, double x, double y);
[[nodiscard]] double loss(const Params& p, double x, double y); // forward(...).loss

// Analytic gradients of J at p:
//     dJ/dc = e,   dJ/dv = e·h,   dJ/db = e·v·(1 − h²),   dJ/dw = dJ/db · x
[[nodiscard]] Grads backward(const Params& p, double x, double y);

// Simultaneous gradient-descent step: every parameter moves by −lr times its gradient, and all
// gradients were computed at the original p (the caller passes backward(p, ...)).
[[nodiscard]] Params sgdStep(const Params& p, const Grads& g, double lr);

// The brief's experiment: inputs, initial parameters, learning rate and training length.
struct BackpropProblem {
    double x = 1.0;
    double y = 2.0;
    Params init{0.5, 0.0, 1.0, 0.0};
    double lr = 0.1;
    int steps = 100;

    static constexpr int kMaxSteps = 1'000'000;

    // All values finite; lr > 0; 1 <= steps <= kMaxSteps.
    // Errors: NonFinite (a NaN/inf value), InvalidInput (lr <= 0 or steps < 1),
    // LimitExceeded (steps > kMaxSteps). The message names the value.
    [[nodiscard]] Result<void> validate() const;
};

} // namespace uniforge::ml
