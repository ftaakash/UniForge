#include "core/solvers/Observers.hpp"

#include <cassert>

namespace uniforge::solvers {

void ErrorVsReference::onIteration(int /*k*/, double /*relResidual*/, const Vector& x) {
    assert(x.size() == reference_.size());
    errors_.push_back(sparse::maxAbsDiff(pool_, x, reference_));
}

int ErrorVsReference::firstIterationBelow(double target) const noexcept {
    for (std::size_t i = 0; i < errors_.size(); ++i) {
        if (errors_[i] <= target) {
            return static_cast<int>(i) + 1;
        }
    }
    return -1;
}

void Snapshot::onIteration(int k, double /*relResidual*/, const Vector& x) {
    if (k % every_ == 0) {
        shots_.push_back({k, x});
    }
}

} // namespace uniforge::solvers
