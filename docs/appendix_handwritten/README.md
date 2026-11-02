# Handwritten appendix

My handwritten backpropagation derivation (Task F), scanned from paper. Nothing in this folder
is generated.

| File | Content |
|---|---|
| `handwritten_appendix.pdf` (2 pages) | Page 1: model, given values, forward pass (z = 0.5, h = ŷ = 0.462117, e = −1.537883, J = 1.182542). Page 2: local derivatives and the chain rule for ∂J/∂c = e, ∂J/∂v = e·h, ∂J/∂b = e·v·(1 − h²), ∂J/∂w = e·v·(1 − h²)·x |

The numeric gradient values, the finite-difference check and the one-step update are computed
and verified in code: `results/csv/backprop_gradients.csv`, `backprop_update.csv` and
[../backprop.md](../backprop.md). The full list of what the derivation covers is in
[CHECKLIST.md](CHECKLIST.md).
