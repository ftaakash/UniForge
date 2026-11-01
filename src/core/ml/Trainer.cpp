#include "core/ml/Trainer.hpp"

#include <cstddef>

namespace uniforge::ml {

std::vector<double> train(const Params& init, double x, double y, double lr, int steps,
                          Params* finalParams) {
    const int n = steps > 0 ? steps : 0;
    std::vector<double> losses;
    losses.reserve(static_cast<std::size_t>(n) + 1U);
    Params p = init;
    losses.push_back(loss(p, x, y));
    for (int k = 0; k < n; ++k) {
        p = sgdStep(p, backward(p, x, y), lr);
        losses.push_back(loss(p, x, y));
    }
    if (finalParams != nullptr) {
        *finalParams = p;
    }
    return losses;
}

} // namespace uniforge::ml
