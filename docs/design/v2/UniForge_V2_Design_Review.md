# UniForge V2: Design Review of the Analysis and Plan

> **Historical input, kept as written (note added in V2-10, 2026-11-23).** This document predates the
> build and was superseded by `UniForge_V2_Build_Spec.md`. The estimates and unchecked facts
> (tags EST and VERIFY below) that the build relied on were carried into the Build Spec and
> resolved there in V2-10 (measured with its result file, verified with its source, or marked
> not used in the report); the others were not used.
> Measured outcomes are in `docs/v2_experiments.md`; the report contains no unresolved marker.

Review of `UniForge_V2_Analysis_and_Plan.md` (2026-11-15), produced with `/design:design-system`
in audit mode, focused on **cost and robustness**: time, memory, iteration count, behaviour under
mesh refinement and behaviour on badly conditioned setups. The plan is reviewed as a system whose
"components" are the geometry, the mesher, the three solvers, the experiment harness and the
report's visual language.

Same tags as the plan: **[EST]** = estimate from theory or rough arithmetic, must be measured
before it reaches the report; **[VERIFY]** = unchecked fact or citation. The scores are my
judgement, not measurements.

---

## Design System Audit

### Summary

**Components reviewed:** 9 (geometry, mesher, FEA/BCs, Solver A, Solver B, Solver C, experiment
protocol, phase plan, figure design) | **Issues found:** 16 (5 high, 7 medium, 4 low) |
**Score:** 80/100 as written, about 92/100 with the amendments below (judgement).

The plan picks the right object and the right three solver families, and its hypotheses cover the
asked-for axes. Its weak points: (1) the mesh ladder gives the direct solver only two data points;
(2) the stopping criterion can report "converged" on the near-singular convection-only problem
while the temperatures are still wrong; (3) assembly, not the solvers, would set the peak memory;
(4) the machine time for the experiments is not budgeted and would probably exceed the coding
time; (5) the phase order verifies the FEA before any solver exists that can handle MMS-sized meshes.

### Naming consistency

| Issue | Where | Recommendation |
|---|---|---|
| "Cube", "bar", "rod", "L0", "LC0" all name the v1 object | Plan §1.1, §2.3, §5 | Glossary in the report: **v1 bar** (object) = ladder level **L0** = load case **LC0**; say once that the notice calls it "the cube" |
| Solver names vary ("Solver A", "direct", "Cholesky", "sparse direct") | §3, §5, §6 | Fixed labels used in every table, plot legend and file name: `DIRECT-ND`, `DIRECT-RCM`, `PCG-J`, `PCG-IC0`, `AMG`, `AMG-PCG` |
| "Iterations" means CG steps for B and V-cycles for C, which cost different amounts | §5.3 | Report iterations **and** work in SpMV-equivalents (amendment A9) |
| "r" (integer refinement factor) vs "s" (spacing) | §4.3, §5.2 | Use spacing s only, with a geometric ladder (amendment A1) |

### Token coverage (figure design system for the PDF)

The notice says "pictorial representations are essential", so the figures are the product's UI.
The plan lists 18 figures but sets no shared visual tokens, so the risk is 18 figures in 18 styles.

| Category | Defined in plan | Gap → token to adopt |
|---|---|---|
| Solver colours | None | One colour per solver family across **every** plot, colour-blind safe (Okabe–Ito palette [VERIFY hex values]): direct = blue `#0072B2`, PCG = orange `#E69F00`, AMG = green `#009E73`; reference/theory lines = grey dashed |
| Variant encoding | None | Variant = line style/marker within the family colour: ND solid ●, RCM dashed ○; Jacobi solid ●, IC(0) dashed ■; AMG standalone solid ▲, AMG-PCG dashed △ |
| Temperature colormap | v1 uses inferno for point clouds | inferno for every temperature field, legend always in °C with min/max ticks; same range across views of the same case |
| Axes | Not specified | Log–log for every scaling plot, decades labelled 10ⁿ, fitted slope printed next to each line, x axis always "DOF" (not r or s) |
| Status markers | Not specified | Failed / over-budget runs drawn as hollow ✕ at the last attempted size with a label ("exceeds 8 GB"), never just missing |
| Typography | v1 report: Inter + JetBrains Mono | Reuse; numbers in tables in monospace, units in every header |
| Captions | Not specified | Template: *what is shown · case/mesh · what to notice · data file name* |

### Component completeness

| Component | Cost model | Robustness / failure handling | Verification | Docs/figures | Score |
|---|---|---|---|---|---|
| Geometry HS-7 + ladder | ✅ | ✅ | ✅ | ✅ | 9/10 |
| Mesher (tensor grid + mask) | ⚠️ sliver cells, uneven aspect ratio (I4) | ⚠️ | ✅ | ✅ | 7/10 |
| FEA / BCs | ⚠️ assembly memory (I3) | ✅ energy balance | ✅ | ✅ | 7/10 |
| Solver A (direct) | ⚠️ too few points (I1) | ⚠️ pivot/accuracy on near-singular K (I8) | ✅ | ✅ | 7/10 |
| Solver B (PCG) | ✅ | ❌ stopping test unsafe at low Biot (I2); IC(0) breakdown underrated (I6) | ✅ | ✅ | 6/10 |
| Solver C (AMG) | ⚠️ setup cost not separated per load case | ⚠️ smoother must be symmetric for AMG-PCG (I9) | ✅ | ✅ | 7/10 |
| Experiment protocol | ❌ no machine-time budget (I5) | ⚠️ hybrid P/E cores (I7); reference at large n (I10) | ✅ | ✅ | 6/10 |
| Phase plan | ⚠️ effort calibration (I12) | – | ❌ FEA verified before a solver exists (I11) | ✅ | 6/10 |
| Figure design | – | – | – | ⚠️ no tokens (above) | 6/10 |

---

## 1. Effectiveness: will the plan actually separate the solvers on cost and robustness?

### 1.1 Expected behaviour per solver (theory + rough arithmetic, all [EST])

Rough sizes for HS-7 (volume 2.416e-5 m³ from v1's STL, i.e. ~2.4 × 10⁴ cells at 1 mm; fins add
~1.4–1.6 nodes per cell because they are only a few cells thick):

| Spacing s | Cells [EST] | DOF n [EST] | nnz(K) ≈ 27n | CSR bytes ≈ 328·n |
|---|---:|---:|---:|---:|
| 1.0 mm | 2.5e4 | 3.5e4 | 9.5e5 | 11 MB |
| 0.5 mm | 2.0e5 | 2.4e5 | 6.5e6 | 79 MB |
| 0.25 mm | 1.6e6 | 1.7e6 | 4.6e7 | 0.56 GB |
| 0.2 mm | 3.0e6 | 3.2e6 | 8.6e7 | 1.05 GB |

| Dimension | Direct (ND) | PCG-Jacobi | PCG-IC(0) | AMG / AMG-PCG |
|---|---|---|---|---|
| **Time vs n** (3D theory) | factor O(n²), solve O(n^{4/3}) [VERIFY George 1973] | O(n^{4/3}): iterations ∝ n^{1/3} × O(n) per iteration | same order, smaller constant (~2–3× fewer iterations [EST]) | O(n) if iterations stay flat; setup ≈ several solves |
| **Memory vs n** | nnz(L) = O(n^{4/3}); at 2.4e5 DOF about 1.5e8–4.5e8 entries → 1.8–5.4 GB [EST, constant 10–30 assumed] | K + 5 vectors ≈ 370·n bytes | + lower triangle ≈ +170·n | K hierarchy (operator complexity 1.3–1.8 [EST]) + P ≈ 550–700·n |
| **Iterations vs n** | none (fixed work) | grows ~n^{1/3}: hundreds to a few thousand at 10⁶ DOF [EST] | same growth | ~flat, 10–40 [EST] |
| **Badly conditioned: low Biot (convection-only)** | cost unchanged; forward error ~κ·ε grows (still small in double) | iterations grow ~1/√Bi in the worst case [EST]; **residual test becomes unreliable** (I2) | same, plus breakdown risk (I6) | near-nullspace (constant T) is built into the aggregates → mostly insensitive |
| **Badly conditioned: k contrast (TIM)** | unchanged | iterations grow; Jacobi partly compensates for contrast between elements | better than Jacobi | robust if aggregates do not cross the material jump (strength filter) |
| **Badly conditioned: aspect ratio (thin TIM cells)** | unchanged | iterations grow | IC(0) may break down: Q1 hexes with aspect ratio > √2 produce **positive off-diagonals** [VERIFY], so K is not an M-matrix | known weak spot of aggregation; strength threshold and AMG-PCG mitigate |
| **Many load cases** | factor once, each extra solve cheap | each solve costs a full solve | IC(0) factor reused | hierarchy reused, each solve costs a full solve |

**Verdict:** the planned experiments (E2 refinement, E3 conditioning, E4 ladder, E5 load cases) do
cover every axis in this table, so the plan *can* separate the solvers. Five issues would weaken or
falsify the evidence; they are listed next.

### 1.2 Issues

| # | Sev. | Issue | Why it matters | Amendment |
|---|---|---|---|---|
| I1 | **High** | Integer refinement r = 1, 2, 3 … multiplies DOF by 8, 27, 64. The direct solver's memory wall is around 2–5 × 10⁵ DOF [EST], so it would get only **two points** (r = 1, 2). A slope cannot be fitted from two points and the crossover cannot be located | The main result (crossover n₁) needs ≥ 4 points per solver below it | **A1** |
| I2 | **High** | Convection-only HS-7 at low Biot is near-singular. A relative residual of 1e-10 does not bound the error: ‖e‖ ≤ κ·‖r‖/‖K‖ in the relevant norms. The **mean temperature level** (set by the tiny convection terms) converges last, so PCG can stop with all temperatures off by a constant | The "same answer" claim and every time-to-solution figure would be wrong exactly where solvers differ most | **A2, A3** |
| I3 | **High** | COO assembly stores 64 triplets per hex: 3 × 10⁶ cells × 64 × 16 B ≈ 3 GB, plus the sort buffer ≈ 6 GB [EST]. Assembly would dominate peak memory and blur the solvers' memory comparison | Memory is one of the four requested metrics | **A4** |
| I4 | Medium | "Mandatory grid lines + fill at spacing s" creates sliver cells where a fill line lands close to a feature line; the TIM aspect ratio s/t_TIM also changes with s, so the refinement sweep mixes two effects (size and conditioning) | Uncontrolled conditioning noise in E2 | **A5** |
| I5 | **High** | No machine-time budget. Example: PCG-J at 3 × 10⁶ DOF streams ~1 GB per SpMV (≈ 50–100 ms single core [EST]) × 1 000–3 000 iterations ≈ 1–5 min per solve; × 5 repetitions × variants × sweep points × E3 cases ≈ many hours | v1 actuals (13.7 h coding vs 46 h planned) suggest **machine time, not coding, is the V2 bottleneck** | **A6** |
| I6 | Medium | IC(0) breakdown rated "low–medium"; with aspect ratio ~10 in the TIM, positive couplings make breakdown likely | A solver variant that silently shifts or fails changes the ranking | **A7** |
| I7 | Medium | i5-14600K is hybrid (6 P + 8 E cores). A single-thread run that lands on an E-core is much slower [EST], and v1's performance doc already notes P/E effects | Serial solver ranking could flip by scheduling luck | **A8** |
| I8 | Medium | "Direct does not care about conditioning" is true for cost, not for accuracy; with ND the near-null mode concentrates in the last separator pivots | Overclaim risk in the report | Reword H4; measure forward error vs κ (A3) |
| I9 | Medium | AMG-PCG needs a symmetric positive definite preconditioner: forward Gauss–Seidel before and **backward** after (symmetric V-cycle), same number of sweeps, exact or SPD coarse solve | A non-symmetric V-cycle makes CG unreliable | Spec it in V2-6, plus a test that checks symmetry of the preconditioner (xᵀM⁻¹y = yᵀM⁻¹x on random vectors) |
| I10 | Medium | "Error vs direct" is impossible above the direct wall | Accuracy target undefined for the most important (largest) points | **A2** (reference = most robust solver to 1e-12, cross-checked against direct where both run) |
| I11 | Medium | Phase V2-3 verifies the FEA (MMS slope 2, fin theory) before any sparse solver exists; the dense oracle stops at a few thousand DOF | Verification would be squeezed onto meshes too coarse for clean slopes | **A10** (reorder phases) |
| I12 | Low | 64 h coding estimate ignores v1's calibration (actual ≈ 0.3 × planned) but also ignores machine time and figure iteration | Misleading schedule both ways | **A6**, re-estimate in §3 |
| I13 | Medium | H1 ("on the cube all solvers look alike") is likely **false for the v1 bar**: a 100:1 slender bar is quasi-1D, so RCM/direct is almost free (bandwidth = one cross-section) while CG needs iterations ∝ L/h (κ ∝ (L/h)²) | The report's opening argument would rest on a hypothesis the data might refute | **A11** |
| I14 | Low | Spy plots of L with 10⁷–10⁸ entries cannot be SVG point plots | Huge PDF, slow render | Rasterise density into pixel bins (PNG) |
| I15 | Low | Peak working set "from a fresh process per run" needs the harness to spawn a process per run; not in the phase plan | Hidden work in V2-7 | Add to V2-7 scope |
| I16 | Low | No failure-mode catalogue: what each solver does when it fails is part of robustness | The notice asks which solver "works best for different scenarios"; failure is a scenario | **A12** |

---

## 2. Amendments

| ID | Amendment | Fixes | Cost |
|---|---|---|---|
| **A1** | **Geometric mesh ladder by spacing:** s_k = s₀ · 2^(−k/3), so DOF roughly **doubles** per step (s = 1.0, 0.79, 0.63, 0.5, 0.40, 0.31, 0.25, 0.20 mm). Gives ≥ 4 direct points before its wall and 8+ points for iterative solvers over two decades; slopes fitted by least squares on log–log | I1 | Free (mesher already takes s) |
| **A2** | **Fair accuracy target = time-to-accuracy.** Per mesh, compute a reference T_ref (direct where it fits; otherwise AMG-PCG to relative residual 1e-12, cross-checked against direct on the overlapping sizes). Run every iterative solver to a tight tolerance while recording ‖T_k − T_ref‖∞ each iteration (O(n), cheap), then report **time and iterations to reach 1 % of the estimated discretisation error** (Richardson estimate of T_max between neighbouring meshes) and to 1e-3 °C. Plot error vs time, not only residual vs iteration | I2, I10 | Small |
| **A3** | **Make conditioning a measured quantity.** (a) Estimate κ(D⁻¹K) from the CG–Lanczos tridiagonal that PCG produces for free; plot iterations vs √κ_est. (b) Use the **global energy balance as a solver-quality check**: the sum of residual entries equals the heat imbalance (1ᵀr = Σq − convected heat), so a solver that stopped with the mean temperature wrong shows a visible watt-level imbalance. Report it per solve | I2, I8 | Small |
| **A4** | **Assemble straight into CSR:** build the sparsity pattern from grid connectivity (each node's ≤ 27 neighbours are known from the tensor grid), allocate once, scatter element matrices with a per-element local→CSR index map. Peak assembly memory ≈ final K. Report assembly time/memory separately from solver numbers | I3 | ~1 h more than COO, saves GBs |
| **A5** | **Graded tensor grid without slivers:** between consecutive mandatory lines use n = ceil(gap/s) equal cells; enforce a max neighbour-size ratio (e.g. 1.5 [EST]) by grading. Treat TIM thickness as its **own** E3 variable (cells through thickness fixed at 1 and 2) so E2 changes size, not conditioning. Record max/mean aspect ratio per mesh | I4 | Small |
| **A6** | **Machine-time budget and run policy.** Repetitions by runtime: 5 if < 10 s, 3 if < 60 s, 1 above (noise is small relative to such times; state it). Per-run caps: 30 min wall time, 10 000 iterations, 8 GB solver memory; a capped run is plotted as ✕ with its reason. Plan the campaign as an overnight batch (target ≤ 8 h [EST]) with a dry-run at the two smallest sizes first. E3 at one mid mesh (~2–3 × 10⁵ DOF) only | I5, I12 | Saves hours, adds ~1 h harness work |
| **A7** | **IC(0) robustness:** detect a non-positive pivot, retry with diagonal shift α = 1e-3, 1e-2, … [VERIFY Manteuffel 1980], record the α used and the number of retries; present breakdown itself as a finding of E3 (aspect-ratio axis). Keep PCG-J as the always-works variant | I6 | Small |
| **A8** | **Pin serial runs to a P-core** (thread affinity mask on one P-core, recorded in metadata), run threaded E6 with and without E-cores. Machine idle check as in v1 | I7 | Small |
| **A9** | **Work units:** report each iteration's cost in SpMV-equivalents (AMG V-cycle ≈ operator complexity × (smoothing sweeps + residual + transfer) [EST]) and the bytes moved per solve, so "iterations" are comparable across families and the bandwidth-bound nature of all three is visible | Naming issue, H7 | Small |
| **A10** | **Reorder phases:** sparse infra → mesher → **PCG-J first** (smallest solver, needed for verification) → FEA + verification on meshes up to ~10⁵ DOF → direct → IC(0) → AMG → experiments. PCG-J is then tested twice: as the verification workhorse and as Solver B | I11 | Free |
| **A11** | **E0 with two simple objects:** the v1 bar (slender, quasi-1D) **and** a true cube of the same volume and physics. Expected [EST]: on the cube all three are close; on the bar direct wins clearly. Either way the report gains its first lesson, "shape alone changes the best solver", before the heat sink appears. Rephrase H1 accordingly | I13 | Small |
| **A12** | **Failure-mode catalogue** (a table and a figure in the report): direct → non-positive pivot / predicted fill over budget (detected before numeric phase); PCG → pᵀKp ≤ 0, stagnation (no 10× residual drop in N iterations), max iterations; IC(0) → pivot breakdown + shift; AMG → residual growth per cycle > 1, coarse solve failure. Plus one **deliberately singular case** (all faces insulated, no Dirichlet) to show each solver detects and reports it rather than returning garbage | I16 | ~2 h |
| A13 | Figure design tokens (table in the audit above) fixed in `docs/report_v2/figure_tokens.md` before V2-8 | Token gaps | Small |
| A14 | Spy plots as density rasters (PNG), not vector points | I14 | Small |
| A15 | AMG-PCG preconditioner symmetry test and symmetric V-cycle spec | I9 | Small |

### Amended hypotheses (replace plan §5.1 where different)

| ID | Amended hypothesis [EST] |
|---|---|
| H1′ | On a compact cube the three solvers give identical temperatures and similar times; on the slender v1 bar the direct solver wins clearly because the problem is quasi-1D |
| H2′ | Direct (ND) wins below n₁; AMG-PCG wins above n₁; n₁ is located by ≥ 4 direct points (A1) |
| H4′ | Lower Biot and higher TIM contrast raise κ_est (A3) and PCG iterations ~√κ_est; AMG iterations rise little; direct **cost** is unchanged and its forward error grows with κ but stays far below the discretisation error |
| H8 (new) | IC(0) breaks down without a shift once the TIM aspect ratio exceeds a threshold [EST ≈ √2 positivity limit]; Jacobi never breaks down |

---

## 3. Re-estimated effort and machine time

| Item | Plan | Reviewed estimate [EST] | Reason |
|---|---:|---:|---|
| Coding + tests (V2-1 … V2-6) | 41 h | 15–25 h | v1 ran at ~0.3× planned; AMG and direct are the uncertain parts |
| Harness + amendments A2–A12 | 6 h | 6–8 h | Process-per-run, caps, time-to-accuracy, failure modes |
| **Machine time (official runs)** | not planned | **6–10 h, overnight, plus a 1 h dry run** | A6 budget |
| Figures + tokens | 7 h | 6–8 h | 18 figures; renders need the rasteriser |
| Report + logs + checks | 8 h | 6–8 h | 18–25 pages |
| **Total working time** | 64 h | **~35–50 h + one overnight run** | |

---

## Priority actions

1. **A1 + A2 + A3**: geometric mesh ladder, time-to-accuracy against a reference, measured κ and
   energy-balance check. Without these the central comparison (crossover and robustness) is either
   under-sampled or can be wrong on the hardest cases.
2. **A6 + A8 + A4**: budget the machine time, pin serial runs to a P-core, assemble directly into
   CSR. These make the timing and memory numbers trustworthy and the campaign finishable.
3. **A10 + A11 + A12**: reorder phases so verification has a solver, start the report with the
   bar-vs-cube lesson, and show failure modes as a robustness result.

## Open questions for your review

- Is a 30 min per-run cap and an 8 GB solver budget acceptable on this machine (32 GB)?
- Should E0 compare against a compact cube (A11), or only the v1 bar as the notice's "cube"?
- The deadline (plan D1) still decides whether E6 (threads), L4 (pin fins) and the optional items survive.
