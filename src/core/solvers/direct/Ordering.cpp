#include "core/solvers/direct/Ordering.hpp"

#include <algorithm>

namespace uniforge::solvers {

namespace {

using sparse::Offset;
using sparse::toSize;

// Breadth-first searches restricted to one subgraph: the nodes whose label equals `id`.
class SubgraphSearch {
public:
    explicit SubgraphSearch(const CsrMatrix& A)
        : a_(A)
        , label_(toSize(A.rows), 0)
        , seen_(toSize(A.rows), 0) {}

    std::vector<int>& labels() { return label_; }
    int newLabel() { return ++lastLabel_; }

    // Neighbours of v inside subgraph `id` (v itself excluded).
    template <class Fn> void forNeighbours(Index v, int id, Fn&& fn) const {
        for (Offset k = a_.rowPtr[toSize(v)]; k < a_.rowPtr[toSize(v) + 1]; ++k) {
            const Index w = a_.col[toSize(k)];
            if (w != v && label_[toSize(w)] == id) {
                fn(w);
            }
        }
    }

    Index degree(Index v, int id) const {
        Index d = 0;
        forNeighbours(v, id, [&](Index) { ++d; });
        return d;
    }

    // Level structure from `start`: `order` gets the nodes level by level, `levelStart` the index
    // in `order` where each level begins (plus a final entry = order.size()). If `byDegree`,
    // the neighbours of each node are visited in increasing degree (Cuthill–McKee order).
    void levels(Index start, int id, bool byDegree, std::vector<Index>& order,
                std::vector<std::size_t>& levelStart) {
        ++stamp_;
        order.clear();
        levelStart.clear();
        order.push_back(start);
        seen_[toSize(start)] = stamp_;
        std::size_t head = 0;
        std::vector<Index> next;
        while (head < order.size()) {
            levelStart.push_back(head);
            const std::size_t end = order.size();
            for (; head < end; ++head) {
                next.clear();
                forNeighbours(order[head], id, [&](Index w) {
                    if (seen_[toSize(w)] != stamp_) {
                        seen_[toSize(w)] = stamp_;
                        next.push_back(w);
                    }
                });
                if (byDegree) {
                    std::stable_sort(next.begin(), next.end(), [&](Index x, Index y) {
                        return degree(x, id) < degree(y, id);
                    });
                }
                order.insert(order.end(), next.begin(), next.end());
            }
        }
        levelStart.push_back(order.size());
    }

    // Pseudo-peripheral node of the component containing `start` (George–Liu iteration).
    Index pseudoPeripheral(Index start, int id) {
        std::vector<Index> order;
        std::vector<std::size_t> levelStart;
        levels(start, id, false, order, levelStart);
        std::size_t depth = levelStart.size();
        for (int it = 0; it < 16; ++it) {
            // Smallest-degree node of the last level.
            Index best = order[levelStart[levelStart.size() - 2]];
            for (std::size_t i = levelStart[levelStart.size() - 2]; i < order.size(); ++i) {
                if (degree(order[i], id) < degree(best, id)) {
                    best = order[i];
                }
            }
            std::vector<Index> order2;
            std::vector<std::size_t> levelStart2;
            levels(best, id, false, order2, levelStart2);
            if (levelStart2.size() <= depth) {
                break;
            }
            start = best;
            depth = levelStart2.size();
            order = std::move(order2);
            levelStart = std::move(levelStart2);
        }
        return start;
    }

    // Connected components of the subgraph made of `nodes` (all labelled `id`).
    std::vector<std::vector<Index>> components(const std::vector<Index>& nodes, int id) {
        ++stamp_;
        const int compStamp = stamp_;
        std::vector<std::vector<Index>> comps;
        for (const Index s : nodes) {
            if (seen_[toSize(s)] == compStamp) {
                continue;
            }
            std::vector<Index> comp{s};
            seen_[toSize(s)] = compStamp;
            for (std::size_t head = 0; head < comp.size(); ++head) {
                forNeighbours(comp[head], id, [&](Index w) {
                    if (seen_[toSize(w)] != compStamp) {
                        seen_[toSize(w)] = compStamp;
                        comp.push_back(w);
                    }
                });
            }
            comps.push_back(std::move(comp));
        }
        return comps;
    }

    // Reverse Cuthill–McKee numbering of the subgraph `nodes`, appended to `out`.
    void appendRcm(const std::vector<Index>& nodes, int id, std::vector<Index>& out) {
        for (const auto& comp : components(nodes, id)) {
            Index start = comp.front();
            for (const Index v : comp) {
                if (degree(v, id) < degree(start, id)) {
                    start = v;
                }
            }
            start = pseudoPeripheral(start, id);
            std::vector<Index> order;
            std::vector<std::size_t> levelStart;
            levels(start, id, true, order, levelStart);
            out.insert(out.end(), order.rbegin(), order.rend());
        }
    }

private:
    const CsrMatrix& a_;
    std::vector<int> label_;
    std::vector<int> seen_;
    int stamp_ = 0;
    int lastLabel_ = 0;
};

Permutation fromOrder(std::vector<Index> order) {
    Permutation p;
    p.perm = std::move(order);
    p.inverse.assign(p.perm.size(), -1);
    for (std::size_t k = 0; k < p.perm.size(); ++k) {
        p.inverse[toSize(p.perm[k])] = static_cast<Index>(k);
    }
    return p;
}

std::vector<Index> allNodes(Index n) {
    std::vector<Index> v(toSize(n));
    for (Index i = 0; i < n; ++i) {
        v[toSize(i)] = i;
    }
    return v;
}

void dissect(SubgraphSearch& g, const std::vector<Index>& nodes, int id, int leafSize,
             std::vector<Index>& out) {
    if (nodes.size() <= static_cast<std::size_t>(leafSize)) {
        g.appendRcm(nodes, id, out);
        return;
    }
    auto comps = g.components(nodes, id);
    if (comps.size() > 1) {
        for (auto& comp : comps) {
            const int sub = g.newLabel();
            for (const Index v : comp) {
                g.labels()[toSize(v)] = sub;
            }
            dissect(g, comp, sub, leafSize, out);
        }
        return;
    }
    // One component: separator = the level where the running count passes half the nodes.
    Index start = nodes.front();
    for (const Index v : nodes) {
        if (g.degree(v, id) < g.degree(start, id)) {
            start = v;
        }
    }
    start = g.pseudoPeripheral(start, id);
    std::vector<Index> order;
    std::vector<std::size_t> levelStart;
    g.levels(start, id, false, order, levelStart);
    const std::size_t nLevels = levelStart.size() - 1;
    if (nLevels < 3) {
        g.appendRcm(nodes, id, out); // too shallow to split
        return;
    }
    std::size_t sep = 1;
    while (sep + 2 < nLevels && levelStart[sep + 1] < order.size() / 2) {
        ++sep;
    }
    const int left = g.newLabel();
    const int right = g.newLabel();
    std::vector<Index> partA(order.begin(),
                             order.begin() + static_cast<std::ptrdiff_t>(levelStart[sep]));
    std::vector<Index> separator(order.begin() + static_cast<std::ptrdiff_t>(levelStart[sep]),
                                 order.begin() + static_cast<std::ptrdiff_t>(levelStart[sep + 1]));
    std::vector<Index> partB(order.begin() + static_cast<std::ptrdiff_t>(levelStart[sep + 1]),
                             order.end());
    for (const Index v : partA) {
        g.labels()[toSize(v)] = left;
    }
    for (const Index v : partB) {
        g.labels()[toSize(v)] = right;
    }
    for (const Index v : separator) {
        g.labels()[toSize(v)] = -1; // numbered last, outside every later subgraph
    }
    dissect(g, partA, left, leafSize, out);
    dissect(g, partB, right, leafSize, out);
    out.insert(out.end(), separator.begin(), separator.end());
}

} // namespace

Permutation naturalOrdering(Index n) {
    return fromOrder(allNodes(n));
}

Permutation rcmOrdering(const CsrMatrix& A) {
    SubgraphSearch g(A);
    std::vector<Index> out;
    out.reserve(toSize(A.rows));
    g.appendRcm(allNodes(A.rows), 0, out);
    return fromOrder(std::move(out));
}

Permutation nestedDissectionOrdering(const CsrMatrix& A, int leafSize) {
    SubgraphSearch g(A);
    std::vector<Index> out;
    out.reserve(toSize(A.rows));
    dissect(g, allNodes(A.rows), 0, std::max(leafSize, 3), out);
    return fromOrder(std::move(out));
}

Permutation makeOrdering(Ordering ordering, const CsrMatrix& A, int leafSize) {
    switch (ordering) {
    case Ordering::Rcm:
        return rcmOrdering(A);
    case Ordering::NestedDissection:
        return nestedDissectionOrdering(A, leafSize);
    case Ordering::Natural:
        break;
    }
    return naturalOrdering(A.rows);
}

bool isValid(const Permutation& p, Index n) {
    if (p.perm.size() != toSize(n) || p.inverse.size() != toSize(n)) {
        return false;
    }
    std::vector<char> hit(toSize(n), 0);
    for (std::size_t k = 0; k < p.perm.size(); ++k) {
        const Index old = p.perm[k];
        if (old < 0 || old >= n || hit[toSize(old)] != 0 ||
            p.inverse[toSize(old)] != static_cast<Index>(k)) {
            return false;
        }
        hit[toSize(old)] = 1;
    }
    return true;
}

} // namespace uniforge::solvers
