# Handwritten backprop appendix: checklist (Task F, F1)

The brief asks for a **handwritten** derivation of at most **2 pages** (4 of the 7 Task F marks).
I write it myself; this file only lists what it must contain and the values to check my working
against. Scan or photograph the pages at ≥ 200 dpi and save them here as
`backprop_page1.pdf` / `backprop_page2.pdf` (or `.png`).

Data: x = 1, y = 2, w = 0.5, b = 0, v = 1, c = 0, lr = 0.1.

## Must contain

- [ ] **Model** written out: z = wx + b, h = tanh z, ŷ = vh + c, e = ŷ − y, J = ½e².
- [ ] **Forward pass** with numbers:
  - z = 0.5
  - h = tanh 0.5 = 0.462117
  - ŷ = 0.462117
  - e = −1.537883
  - J = 1.182542
- [ ] **Local derivatives**: ∂J/∂ŷ = e, ∂ŷ/∂c = 1, ∂ŷ/∂v = h, ∂ŷ/∂h = v,
      ∂h/∂z = 1 − tanh² z = 1 − h² (= 0.786448), ∂z/∂b = 1, ∂z/∂w = x.
- [ ] **Four chain-rule derivatives**, each as a product of local derivatives, then the number:
  - ∂J/∂c = e = −1.537883
  - ∂J/∂v = e·h = −0.710682
  - ∂J/∂b = e·v·(1 − h²) = −1.209464
  - ∂J/∂w = e·v·(1 − h²)·x = −1.209464
- [ ] **Numeric gradients**: the central-difference formula
      (J(θ + ε) − J(θ − ε)) / 2ε with ε = 1e-6, and the result that all four match the analytic
      values (program output: |diff| ≈ 1e-10, tolerance 1e-5, see `results/csv/backprop_gradients.csv`).
- [ ] **One simultaneous update** (all gradients from the original parameters), θ ← θ − 0.1·∂J/∂θ:
  - w = 0.620946
  - b = 0.120946
  - v = 1.071068
  - c = 0.153788
- [ ] **Gradient-direction explanation**: ŷ < y so e < 0; each gradient is e times a positive
      factor (1, h, v(1 − h²), v(1 − h²)x), so all four are negative; descent therefore
      increases all four parameters, and each increase raises ŷ toward y (loss 1.182542 →
      0.685774 after the step).
- [ ] Name, date and page numbers on each page; ≤ 2 pages in total.

## Before submitting

- [ ] Every number above re-derived by hand (calculator allowed), not copied from this list.
- [ ] I can explain each step without notes. That is the "understand and defend" requirement
      for the verification discussion.
- [ ] Scans are legible and saved in this folder; `README.md` here lists the files.

Full typed derivation and the loss-curve discussion: [../backprop.md](../backprop.md).
