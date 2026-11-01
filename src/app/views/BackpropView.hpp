#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "app/views/View.hpp"
#include "core/ml/GradientCheck.hpp"
#include "core/ml/TinyNet.hpp"

namespace uniforge::views {

// Task F: one-neuron backprop. Editable inputs (brief values by default, reset button), forward
// pass, analytic vs central-difference gradients, one simultaneous update and the loss curve
// with a log-y toggle. The whole computation takes microseconds, so it runs on the UI thread whenever
// an input changes instead of going through JobRunner.
class BackpropView final : public View {
public:
    BackpropView();

    [[nodiscard]] const char* title() const override { return "Backprop"; }
    [[nodiscard]] const char* tag() const override { return "Task F"; }
    [[nodiscard]] const char* purpose() const override {
        return "Hand-derived gradients of a one-neuron network (z = wx + b, h = tanh z, "
               "y_hat = vh + c, J = ½e²), checked by central differences and trained by "
               "gradient descent.";
    }
    void draw(AppContext& ctx) override;

private:
    struct Computed {
        ml::Forward forward;
        ml::GradCheck check;
        ml::Params next;
        std::vector<double> steps, losses;
        std::vector<double> hAt; // h = tanh(z) after each step
        ml::Params last;
        bool nonIncreasing = false;
    };

    void recompute(AppContext& ctx);
    void drawBadges();
    void drawInputs(AppContext& ctx);
    void drawForward();
    void drawGradients();
    void drawUpdate();
    void drawLoss();
    [[nodiscard]] bool isBrief() const;

    ml::BackpropProblem input_;
    std::optional<Error> inputError_;
    std::optional<Computed> result_; // last valid result, kept while an input is invalid
    bool logY_ = true;
};

} // namespace uniforge::views
