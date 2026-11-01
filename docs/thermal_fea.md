# Thermal FEA (Task C): 1D steady conduction in a heated rod

This note explains what the solver in `src/core/thermal/` computes, why the results look the way
they do, and how they were verified. Every number in the tables below is copied from
`results/csv/convergence.csv`, which `uniforge_cli fea` regenerates byte for byte (checked by
the `integration.cli_fea_matches_golden` test).

## 1. The problem

A rod of length L is held at T0 on the left and TL on the right. Heat is generated uniformly
inside it (an electric current, say), and the side surface is insulated, so the only way out
is through the two ends.

**In plain words:** in steady state, every slice of the rod must conduct away exactly the heat
generated inside it. Heat flows from hot to cold at a rate proportional to the temperature
gradient (Fourier's law, flux = −k dT/dx). So the gradient must change along the rod at a
constant rate set by the heat generation, which makes the temperature curve a parabola.

As an equation:

```text
−d/dx( k A dT/dx ) = q A      for 0 < x < L
T(0) = T0,   T(L) = TL        (Dirichlet boundary conditions at both ends)
```

| Symbol | Meaning | Unit | Brief value |
|---|---|---|---|
| L | rod length | m | 1 |
| A | cross-section area | m² | 1e-4 |
| k | thermal conductivity | W/(m·K) | 10 |
| q | volumetric heat generation | W/m³ | 1e4 |
| T0, TL | end temperatures | °C | 100, 20 |
| N | number of elements | – | 10, 20, 40, 80 |
| h = L/N | element length | m | 0.1 … 0.0125 |

Temperatures are in °C throughout; only differences enter the equations, so °C and K give the
same gradients and heat flows.

### Assumptions

1. **Steady state:** nothing changes with time (no heat capacity term).
2. **1D:** temperature is uniform over each cross-section; it varies only with x.
3. **Constant k and A** along the rod and independent of temperature.
4. **Insulated lateral surface:** no convection or radiation from the side.
5. **Uniform q:** the same heat generation everywhere.

Under these assumptions the exact solution is

```text
T(x) = T0 + (TL − T0)·x/L + (q / 2k)·x·(L − x)
     = 100 + 420x − 500x²   °C   (brief values)
```

a straight line between the end temperatures plus a parabola that is zero at both ends.
`thermal::exactTemperature` implements it.

### Why the peak is 188.2 °C at x = 0.42 m

The parabola peaks where dT/dx = 0: `420 − 1000x = 0`, so x* = 0.42 m and
T(0.42) = 100 + 176.4 − 88.2 = **188.2 °C**. In general
x* = L/2 + k(TL − T0)/(qL): without the end-temperature difference the peak would sit in the
middle; the hotter left end pushes it 0.08 m to the left, towards the hot side, because less
heat has to leave through the hot end. Of the 1 W generated, k·A·T'(0) = 0.42 W leaves
on the left and −k·A·T'(L) = 0.58 W on the right. (`thermal::exactPeak`; tested in
`RodSolver.PeakIs188Point2AtX0Point42` on N = 50 and N = 100, which have a node at x = 0.42.)

## 2. Finite-element formulation

### Weak form

Multiply the equation by a test function w(x) that is zero at both ends (where T is
prescribed) and integrate by parts over the rod:

```text
∫₀ᴸ k A (dw/dx)(dT/dx) dx = ∫₀ᴸ w q A dx      for every admissible w
```

The boundary term `[w k A dT/dx]₀ᴸ` drops out because w = 0 at both ends. The weak form only
needs first derivatives, so piecewise-linear functions are good enough.

### Two-node linear element

Split [0, L] into N equal elements of length h. On element e (nodes e and e+1), with the local
coordinate ξ = (x − x_e)/h, the temperature is interpolated linearly:

```text
T(x) ≈ N1(ξ)·T_e + N2(ξ)·T_e+1,     N1 = 1 − ξ,   N2 = ξ,   dN1/dx = −1/h,   dN2/dx = 1/h
```

Putting this into the weak form with w = N1 and w = N2 gives, per element
(`ElementMatrices.hpp`):

```text
Ke = ∫ k A (dNi/dx)(dNj/dx) dx = (k A / h) · [  1  −1 ]     (W/K)
                                             [ −1   1 ]

fe = ∫ Ni q A dx = (q A h / 2) · [ 1 ]                        (W)
                                 [ 1 ]
```

The load vector is the *consistent* one: it is the exact integral of the shape functions times
q, which for uniform q puts half of each element's heat on each of its two nodes.

### Assembly

Element e adds its 2×2 block into rows/columns (e, e+1) of the global matrix, so K is
tridiagonal and stored as three arrays: `a` (below the diagonal), `b` (diagonal) and `c`
(above), with the load in `f` (`Assembler.cpp`). For the brief's N = 10:
interior rows are `[−0.01, 0.02, −0.01]` W/K, end rows have one element (`b = 0.01`); interior
loads are qAh = 0.1 W and the end loads 0.05 W; the loads add up to q·A·L = 1 W.

Before boundary conditions every row of K sums to zero: a uniform temperature produces no heat
flow. K is therefore singular until a temperature is fixed somewhere (test
`Assembler.GlobalStiffnessIsSymmetricWithZeroRowSums`).

### Dirichlet boundary conditions by symmetric elimination

At node 0 the temperature is known, so its column is moved to the right-hand side and its row
is replaced by the identity (`DirichletBC.cpp`):

```text
f[1]   −= a[1]·T0;     a[1]   = 0;     b[0] = 1;   c[0]   = 0;   f[0] = T0
f[N−1] −= c[N−1]·TL;   c[N−1] = 0;     b[N] = 1;   a[N]   = 0;   f[N] = TL
```

Zeroing both `a[1]` (column 0 in row 1) and `c[0]` (row 0) keeps the matrix symmetric, and the
identity rows make `T[0] == T0` and `T[N] == TL` hold **exactly** (bit for bit) after the solve.
Replacing only the boundary row would also give the right answer, but the matrix would no
longer be symmetric.

The unconstrained rows 0 and N are saved before elimination. After the solve they give the
**reactions**, the nodal heat the boundaries must supply: `r0 = b0·T0 + c0·T1 − f0`. The heat
leaving through each end is `−r`, and the energy balance is

```text
heatOutLeft + heatOutRight = q·A·L = 1 W
```

### Solving: Thomas algorithm, no pivoting needed

The Thomas algorithm is Gaussian elimination for three diagonals: one forward sweep
(`m_i = b_i − a_i·c'_{i−1}`, `c'_i = c_i/m_i`, `d'_i = (f_i − a_i·d'_{i−1})/m_i`) and one back
substitution, O(N) work and memory (`ThomasSolver.cpp`). It does no row exchanges, which is safe
here because the reduced matrix is

- **symmetric positive definite** (it is the stiffness of a rod that is held at both ends:
  `Tᵀ K T = Σ (kA/h)(T_e+1 − T_e)² > 0` unless T = 0 on the free nodes), and
- **irreducibly diagonally dominant:** every row has |b_i| ≥ |a_i| + |c_i| (2s vs s + s in the
  interior), the rows next to a boundary are *strictly* dominant (2s vs s, because the coupling
  to the fixed node was eliminated), and the matrix cannot be split into independent blocks.
  Plain diagonal dominance would not be enough (equality everywhere would allow a singular
  matrix); the strict rows plus irreducibility guarantee that every pivot m_i stays positive.

The solver still checks every pivot and returns `ErrorCode::Internal` instead of producing
inf/NaN for some other (singular) input. A dense Gaussian elimination with partial pivoting,
compiled only into the tests (`tests/support/DenseGauss.hpp`), agrees with Thomas to 1e-11 °C on
the rod systems and to 1e-12 (relative) on 20 random diagonally dominant systems.

## 3. Why the results are what they are

### Linear elements are exact at the nodes here

In 1D, the influence of a point heat source at a node (the Green's function of
−kA d²/dx² with both ends fixed) is a piecewise-linear "tent" with its kink at that node.
That function is itself in the finite-element space. Galerkin's method makes the error
orthogonal (in the energy inner product ∫ kA e'v' dx) to every function in the space, so in
particular to each node's Green's function, and that inner product *is* the error at the node.
Therefore the FE nodal temperatures equal the exact ones, for any number of elements, as long as
the load is integrated exactly (true for the consistent load with uniform q). Measured: the
largest nodal error is 1.2e-12 °C (rounding only), see the table.

### The error between nodes is q·h²/(8k), so it converges as O(h²)

Since the nodal values are exact, the FE solution between two nodes is the straight chord
through two points of the exact parabola. The gap between a parabola and its chord is

```text
T(x) − T_fe(x) = (q / 2k)·(x − x_e)·(x_e+1 − x)
```

largest at the element midpoint, where it is `(q/2k)·(h/2)² = q·h²/(8k)`. The chord always lies
*below* the parabola (q > 0), and the gap is the same in every element, because T'' = −q/k is
the same everywhere. Halving h divides the error by 4: the error is O(h²), observed order 2.

The brief's values give q/(8k) = 125 °C/m², so the midpoint errors are 1.25, 0.3125, 0.078125
and 0.01953125 °C for N = 10, 20, 40 and 80.

### Convergence table (measured)

From `results/csv/convergence.csv` (full 17-digit values in the file):

| N | h (m) | max midpoint error (°C) | q·h²/(8k) (°C) | ratio | observed order | max nodal error (°C) | energy residual (W) |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 10 | 0.1 | 1.2500000000000284 | 1.25 | – | – | 2.84e-14 | 2.22e-16 |
| 20 | 0.05 | 0.3125 | 0.3125 | 4.0000000000000906 | 2.0000000000000329 | 1.99e-13 | 1.11e-15 |
| 40 | 0.025 | 0.078124999999964473 | 0.078125 | 4.000000000001819 | 2.0000000000006559 | 8.81e-13 | 5.11e-15 |
| 80 | 0.0125 | 0.019531249999992895 | 0.01953125 | 3.9999999999996363 | 1.999999999999869 | 1.22e-12 | 8.66e-15 |

- Measured and expected midpoint errors agree to 3.6e-14 °C (tolerance 1e-9).
- The ratio is 4 and the observed order `log(ratio)/log(2)` is 2 to within 7e-13 (tolerance 1e-6).
- `T[0] == 100` and `T[N] == 20` exactly on every mesh.
- The energy balance holds to 8.7e-15 W out of 1 W (tolerance 1e-10).
- The nodal "error" grows from 3e-14 to 1.2e-12 °C as N goes from 10 to 80. That is rounding,
  not discretisation error: the condition number of K grows like N², and temperatures of about
  190 °C carry rounding of about 3e-14 °C per operation. It stays four orders of magnitude below
  the 1e-10 tolerance.

Plots: `results/plots/T_vs_x.svg` (all four meshes and the dashed exact curve; the FE nodes lie
on the curve, which is the nodal exactness above) and `results/plots/error_vs_h_loglog.svg`
(measured midpoint error on the dashed slope-2 line q·h²/(8k)).

## 4. How to rerun

```bash
build/windows-msvc/bin/Release/uniforge_cli.exe fea          # writes results/csv and results/plots
ctest --preset windows-release -L numerical                  # numerical checks
ctest --preset windows-release -L integration                # CLI output = tests/golden/fea
```

From the package: double-click `regenerate_evidence.cmd`. The CLI prints the table above and
five PASS/FAIL checks and exits with code 3 if any check fails.

| Test (CTest name) | What it checks |
|---|---|
| `ElementMatrices.*` | Ke = 0.01·[[1,−1],[−1,1]] W/K and fe = 0.05·[1,1] W for N = 10 |
| `Assembler.GlobalStiffnessIsSymmetricWithZeroRowSums` | K symmetric, rows sum to 0, loads sum to q·A·L |
| `DirichletBC.EliminationKeepsSymmetryAndMakesIdentityRows` | symmetric elimination, diagonal dominance (strict next to the BCs) |
| `RodSolverMeshes.*` (N = 1, 2, 10, 20, 40, 80) | exact BCs, nodal error ≤ 1e-10, end heat flows 0.42 / 0.58 W, Thomas = dense oracle |
| `ConvergenceStudy.*` | midpoint error = q·h²/(8k) ± 1e-9 in every element, ratio 4 ± 1e-6, order 2, other parameters too |
| `RodSolver.PeakIs188Point2AtX0Point42` | T_max = 188.2 °C at x = 0.42 m |
| `ThermalInput.*`, `ThermalCancel.*` (label `negative`) | N < 1, N > 1e7, k/A/L ≤ 0, NaN/inf give typed errors; cancellation returns `Cancelled` |
| `integration.cli_fea_matches_golden` | CLI CSVs = golden files; two runs give identical CSVs and SVGs |

## 5. Debugging log

Written while the work happened (2026-11-01), not reconstructed afterwards.

1. **Real build failure, CLI manifest (not in the solver).** The first build of
   `uniforge_cli.exe` failed in the linker's manifest step:
   `general error c1010070: Failed to load and parse the manifest`. The XML comment at the top
   of `src/cli/cli.manifest` mentioned the `--out` option, and XML does not allow two hyphens in
   a row inside a comment. Fix: reworded the comment (it now says why option names are not
   spelled out there).
2. **Real presentation issue, T(x) plot.** In the first version of `T_vs_x.svg` the four meshes
   were drawn coarse to fine. Because the nodal values are exact, all four lie on the same
   curve, and the 81 N = 80 markers covered the N = 10/20/40 markers, so the plot looked like a
   single series. Fix: draw the finest mesh first and the coarse meshes on top (legend order
   follows draw order).
3. **The solver itself showed no bug in this phase.** All 69 new tests passed on the first
   build. Because that proves little on its own, I ran a **deliberate fault-injection test**
   (labelled as such, code restored afterwards):
   - *Injected fault:* in `applyDirichlet`, removed `f[1] −= a[1]·T0`, i.e. zeroed the column of
     the known left temperature without moving its contribution to the right-hand side. This
     is an easy mistake to make, since the matrix still looks correct.
   - *What still passed:* `T[0] == T0` and `T[N] == TL` exactly, and the symmetry checks, so a
     boundary-value check alone does not catch it. The N = 1 case also passed (both rows are
     identities there).
   - *What failed:* 18 of 83 tests. The CLI reported a max nodal error of 90, 95, 97.5 and
     98.75 °C, midpoint "errors" of 86–98 °C with ratio ≈ 0.93–0.98 and order ≈ −0.1, and an
     energy residual of −1, −2, −4 and −8 W. The residual equals the missing term exactly:
     a[1]·T0 = −(kA/h)·T0 = −0.1·N W. In effect, row 1 behaved as if node 0 were at 0 °C.
   - *Lesson:* the energy balance (computed from the *unconstrained* boundary rows) and the
     convergence order find boundary-condition mistakes that exact boundary values hide, which
     is why both are part of the CLI's PASS/FAIL checks.
   - *Also checked:* changing the last digit of one value in `tests/golden/fea/convergence.csv`
     makes `integration.cli_fea_matches_golden` fail with "differs from golden: convergence.csv".
