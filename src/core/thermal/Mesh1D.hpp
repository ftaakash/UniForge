#pragma once

#include <cstddef>
#include <vector>

namespace uniforge::thermal {

// Uniform 1D mesh of `elements` two-node elements on [0, L]. Node i sits at x_i = i·L/N,
// computed directly (not by summing h), so x_0 = 0 and x_N = L exactly and every node that
// should land on a "round" coordinate (e.g. x = 0.42 for N = 50) does.
struct Mesh1D {
    double L = 1.0;
    int elements = 1;

    [[nodiscard]] std::size_t nodeCount() const noexcept {
        return static_cast<std::size_t>(elements) + 1U;
    }
    [[nodiscard]] double h() const noexcept { return L / static_cast<double>(elements); }
    [[nodiscard]] double node(std::size_t i) const noexcept {
        return static_cast<double>(i) * L / static_cast<double>(elements);
    }
    // Fills `x` with the node coordinates, reusing its capacity.
    void nodes(std::vector<double>& x) const {
        x.resize(nodeCount());
        for (std::size_t i = 0; i < x.size(); ++i) {
            x[i] = node(i);
        }
    }
};

} // namespace uniforge::thermal
