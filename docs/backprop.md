# Task F: Backpropagation for a one-neuron network

This note covers the derivation behind `src/core/ml/` and explains the measured loss curve. The
handwritten version for the appendix is mine; [appendix_handwritten/CHECKLIST.md](appendix_handwritten/CHECKLIST.md)
lists what it must contain. All numbers below come from `uniforge_cli backprop` (evidence in
`results/csv/backprop_gradients.csv`, `backprop_update.csv`, `loss_curve.csv`,
`results/plots/loss_curve.svg`, `loss_curve_logy.svg`).

## 1. Model and data

One training sample, one hidden tanh unit, one linear output:

| Symbol | Meaning | Brief value |
|---|---|---|
| x, y | input, target | 1, 2 |
| w, b | hidden weight and bias | 0.5, 0 |
| v, c | output weight and bias | 1, 0 |
| lr | learning rate | 0.1 |

```text
z = w·x + b          h = tanh(z)          ŷ = v·h + c          e = ŷ − y          J = ½·e²
```

## 2. Forward pass

| Quantity | Computation | Value |
|---|---|---|
| z | 0.5·1 + 0 | 0.500000 |
| h | tanh(0.5) | 0.462117 |
| ŷ | 1·0.462117 + 0 | 0.462117 |
| e | 0.462117 − 2 | −1.537883 |
| J | ½·(−1.537883)² | 1.182542 |

## 3. Backward pass (chain rule)

Local derivatives, from the output back to the input:

```text
∂J/∂ŷ = e      ∂ŷ/∂c = 1      ∂ŷ/∂v = h      ∂ŷ/∂h = v
∂h/∂z = 1 − tanh²(z) = 1 − h²             ∂z/∂b = 1      ∂z/∂w = x
```

Each parameter's gradient is the product of the local derivatives along its path to J:

| Parameter | Chain | Formula | Value |
|---|---|---|---|
| c | ∂J/∂ŷ · ∂ŷ/∂c | e | −1.537883 |
| v | ∂J/∂ŷ · ∂ŷ/∂v | e·h | −1.537883 · 0.462117 = −0.710682 |
| b | ∂J/∂ŷ · ∂ŷ/∂h · ∂h/∂z · ∂z/∂b | e·v·(1 − h²) | −1.537883 · 1 · 0.786448 = −1.209464 |
| w | ∂J/∂ŷ · ∂ŷ/∂h · ∂h/∂z · ∂z/∂w | e·v·(1 − h²)·x = (∂J/∂b)·x | −1.209464 · 1 = −1.209464 |

`dw = db` here only because x = 1. The unit tests also check a point with x ≠ 1 and v ≠ 1
(`BackpropGradCheck.PassesAwayFromTheBriefPoint`), so a missing factor x or v would be caught.

The code (`ml::backward`) computes `e` and `h` once in the forward pass and reuses them, which is
exactly what backpropagation is: the shared factor `e·v·(1 − h²)` (the error signal at z) is
computed once and multiplied by `∂z/∂b = 1` and `∂z/∂w = x`. There is no autodiff.

## 4. Numerical gradient check

Central differences, one parameter at a time, with the brief's ε = 1e-6:

```text
numeric_i = ( J(p + ε·e_i) − J(p − ε·e_i) ) / (2ε)      pass if |analytic_i − numeric_i| ≤ 1e-5
```

| Parameter | Analytic | Numeric | \|diff\| | ≤ 1e-5 |
|---|---:|---:|---:|:---:|
| w | −1.2094644752 | −1.2094644751 | 1.13e-10 | ✓ |
| b | −1.2094644752 | −1.2094644751 | 1.13e-10 | ✓ |
| v | −0.7106820475 | −0.7106820474 | 5.25e-11 | ✓ |
| c | −1.5378828427 | −1.5378828426 | 1.12e-10 | ✓ |

The differences are five orders of magnitude below the tolerance, which is what the error
budget predicts. Truncation error of the central difference is O(ε²·J‴) ≈ 1e-12. Round-off is
about 1e-16·J/ε ≈ 1e-10, and that sets the measured ~1e-10. A wrong formula (a missing
factor `v`, `x` or `1 − h²`) would produce differences of order 0.1 to 1.

## 5. One simultaneous update

All four gradients are evaluated at the **original** parameters, then every parameter moves at
once: p ← p − lr·∂J/∂p.

| Parameter | Before | Gradient | After = before − 0.1·gradient |
|---|---:|---:|---:|
| w | 0.5 | −1.209464 | 0.620946 |
| b | 0 | −1.209464 | 0.120946 |
| v | 1 | −0.710682 | 1.071068 |
| c | 0 | −1.537883 | 0.153788 |

A sequential update (change v first, then recompute db with the new v) would give a different
b. `BackpropUpdate.UsesGradientsOfTheOriginalParameters` shows the difference is larger than
1e-3, so the test would catch it.

## 6. Why every gradient is negative, and why that helps

The prediction is too low: ŷ = 0.462 < y = 2, so e < 0. Each gradient is e times a positive
factor: ∂ŷ/∂c = 1, ∂ŷ/∂v = h = 0.462 > 0, and ∂ŷ/∂b = v·(1 − h²) = 0.786 > 0, ∂ŷ/∂w = that
times x = 1 > 0. So all four gradients are negative. Gradient descent subtracts lr·gradient,
so it **increases all four parameters**. Each increase raises ŷ: a larger c or v raises ŷ
directly, and a larger w or b raises z, then h, then ŷ, because tanh is increasing and v > 0.
After one step ŷ = 1.071068·tanh(0.741892) + 0.153788 = 0.828, and J drops from 1.182542 to
0.685774 (measured, `loss_curve.csv` row 1).

## 7. The measured loss curve (100 steps, lr = 0.1)

Selected rows from `uniforge_cli backprop` (full curve in `results/csv/loss_curve.csv`):

| Step | J | J_k / J_k−1 | h | 1 − h² |
|---:|---:|---:|---:|---:|
| 0 | 1.182542 | – | 0.462117 | 0.786448 |
| 1 | 0.685774 | 0.579915 | 0.630287 | 0.602738 |
| 2 | 0.416680 | 0.607605 | 0.712923 | 0.491741 |
| 5 | 0.101320 | 0.628194 | 0.808794 | 0.345852 |
| 10 | 1.021102e-02 | 0.633225 | 0.849024 | 0.279158 |
| 20 | 1.074133e-04 | 0.634484 | 0.862231 | 0.256558 |
| 50 | 1.276077e-10 | 0.634607 | 0.863629 | 0.254144 |
| 100 | 1.702622e-20 | 0.634606 | 0.863631 | 0.254142 |

After 100 steps: w = 0.903729, b = 0.403729, v = 1.492948, c = 0.710644, ŷ = 1.999999999815.

**The loss heads to 0.** There is one equation (ŷ = y) and four unknowns, so a whole
three-dimensional family of parameter sets fits the sample exactly, and the minimum of J is 0.
Gradient descent reaches the member of that family nearest its path from the start. The loss
decreases at **every** one of the 100 steps (strictly, not just non-increasing; checked by
`BackpropTrain.LossDecreasesAtEveryStepAndApproachesZero` and by the CLI) and ends at
1.7e-20, i.e. |e| = 1.8e-10. That is still far above double round-off (ulp(2) ≈ 4.4e-16), so
the curve has not hit a floating-point floor; it would keep falling.

**The rate is linear (geometric).** On the log-y plot the curve becomes a straight line: from
about step 10 on, every step multiplies J by 0.6346, so J falls by a factor of 10 roughly
every 5 steps (1.3e-10 at step 50, 1.7e-20 at step 100: ten decades in fifty steps). This is
what gradient descent does near a minimum. Linearising ŷ around the current parameters,
one step changes the error by

```text
e_new ≈ e · (1 − lr·s),     s = ‖∂ŷ/∂p‖² = (v(1−h²)x)² + (v(1−h²))² + h² + 1,
J_new / J ≈ (1 − lr·s)²
```

At the final parameters s = 2.0338, so (1 − 0.1·2.0338)² = 0.634607. The measured ratio is
0.634606. lr = 0.1 is comfortably stable: the linearised step overshoots once lr·s > 1 and
diverges once lr·s > 2, i.e. lr > 2/2.45 ≈ 0.8 at the start and 2/2.03 ≈ 1.0 at the end
(*from the formula, not measured*).

**tanh saturation slows the hidden layer.** As w and b grow, z rises from 0.50 to 1.31 and
h = tanh z from 0.46 to 0.86. The slope tanh′(z) = 1 − h² falls from 0.786 to 0.254, a factor
of 3.1. The gradients of w and b both carry this factor, so the hidden layer learns about
three times more slowly by the end, while v and c (gradients e·h and e, not behind tanh) are
unaffected. This is visible in the table above: the per-step ratio worsens from 0.58 on the first step
to 0.635 at the end, because s shrinks from 2.45 at the start to 2.03 as 1 − h² drops. It
also shows in the final parameters: c moved by 0.71 and v by 0.49, more than w and b (0.40
each; w − b stays 0.5 because dw = db when x = 1). Here the unit is only mildly saturated (h =
0.86). *Not measured, reasoning only:* with a much larger starting z (say h ≈ 0.999), w and b
would almost stop moving, but c alone can still close the gap, so the loss would still reach 0.

## 8. Where it lives in the code

| What | File |
|---|---|
| forward, backward, sgdStep, BackpropProblem | `src/core/ml/TinyNet.{hpp,cpp}` |
| centralDiff (ε = 1e-6, tolerance 1e-5) | `src/core/ml/GradientCheck.{hpp,cpp}` |
| train (steps + 1 losses) | `src/core/ml/Trainer.{hpp,cpp}` |
| value, FD, update, loss tests | `tests/numerical/ml/BackpropTest.cpp`, `tests/negative/ml/BackpropInputTest.cpp` |
| CLI evidence + golden files | `src/cli/commands/BackpropCommand.cpp`, `tests/golden/backprop/`, `tests/integration/backprop_golden.cmake` |
| GUI | `src/app/views/BackpropView.cpp` |

Rerun: `uniforge_cli backprop --out results`, or `ctest -L numerical -R Backprop`.
