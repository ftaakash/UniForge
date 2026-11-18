# UniForge V2: Analysis and Plan (three solvers on a 3D thermal problem)

Status: **revision 2, nothing built.** Written 2026-11-15 from the Assignment Extension
Notice (1 page) and the v1 repository (`UniForge/`, tag `v1.0.0`, `main` = v1 + CI fixes).
Revision 2 folds in amendments A1–A15 from `UniForge_V2_Design_Review.md` (same day); each changed
place is marked with its amendment ID, e.g. (A2).
Next step per the agreed workflow: run design skills on this plan, compare, pick, then build
phase by phase with review before every push.

Conventions in this document:
- **[EST]** = an estimate or expectation from theory, to be replaced by a measurement. No number
  marked [EST] may go into the report unmeasured.
- **[VERIFY]** = a fact or citation I believe is right but have not checked against the source in
  this session. Every [VERIFY] must be checked (or dropped) before it reaches the PDF.
- Decisions that are yours are collected in §9.

---

## 1. Requirement breakdown

### 1.1 What the notice says (paraphrased, by requirement)

| ID | Requirement | Source wording (short) | Hard / soft |
|---|---|---|---|
| R1 | Introduce the solver concept: apply **three different types** of solvers to the same thermal problem | "apply three different types of solvers" | Hard |
| R2 | Evaluate which solver is the **most efficient / optimal** | "evaluate which solver is the most efficient or optimal" | Hard |
| R3 | If the simple geometry gives similar results because the problem is linear, **change the geometry** to a more complex 3D design | "free to alter the geometry … more complex 3D design" | Effectively hard (see 1.2) |
| R4 | Perform **the same thermal analysis as before** on a **different object** | "same thermal analysis on a different object" | Hard |
| R5 | **Justify how each of the three solvers was built** | "justify how you built each of the three software solutions" | Hard |
| R6 | Justify **which solver works best for which scenario**: trade-off between design complexity and solver selection | "trade-off between design complexity and solver selection" | Hard, the core judgement |
| R7 | Deliverable: **one PDF**, reply to the email, **no source code**, pictorial representations essential, explain in detail | "report only … single PDF … pictorial representations are essential" | Hard |
| R8 | Only this single thermal operation needs to be compared (not a general benchmark of commercial tools) | "only need to demonstrate this comparison for this single thermal operation" | Scope limit |

The notice calls the v1 problem a "simple cube". In v1 it was a 1 m × 10 mm × 10 mm bar solved as a
1D rod (Task C). The report should say this plainly once ("the v1 bar, referred to as the cube in
the notice") and then use the notice's wording.

### 1.2 The key technical point behind R3 (must be answered head-on)

For a **linear, symmetric positive definite** system K·T = f (steady conduction with constant k is
exactly that), the solution is unique. Any correct solver must return **the same temperatures** to
within its tolerance. "Similar results across solvers" on the cube is therefore not a failure, it is
the verification. What separates solvers is **cost and robustness**: time, memory, iteration count,
setup cost, sensitivity to mesh size, element aspect ratio, material contrast and boundary
conditions. On a cube-like bar these are all small and alike, which is why the notice asks for a
harder geometry.

The report wins this point if it shows, with data:
1. On simple objects: three solvers, identical temperatures (Experiment E0). Two simple objects
   are used (A11): a compact **cube**, where the timings are expected to be close, and the slender
   **v1 bar**, which is quasi-1D, so the direct solver is expected to win clearly. Either outcome
   gives the report its first lesson: shape alone can change the best solver.
2. On the complex object: identical temperatures again (verification), but **clearly different and
   differently scaling** cost, and a **measured crossover** where the best solver changes.
3. Why: the geometry drives problem size (DOF), conditioning (thin parts, contrast, low Biot number)
   and sparsity structure (separator size for direct solvers), and each solver family reacts to a
   different one of these.

(Optional extension if time allows, §6.6: a temperature-dependent conductivity k(T) makes the
problem **nonlinear**, where solver choice can also change the path to the answer. Not needed to
satisfy the notice.)

### 1.3 How the report will probably be judged (inferred, no rubric given)

The notice gives no rubric. From its wording and from what an R&D simulation team would look for:

| Criterion (inferred) | Weight guess | What scores highest |
|---|---|---|
| Three solvers genuinely different, correctly implemented | High | One solver from each family: **direct**, **Krylov**, **multigrid**; each shown to converge to the same verified answer |
| "How you built it" justification (R5) | High | For each solver: algorithm, data structures, key design choices and why, complexity, what was written from scratch vs reused, how it was tested. Diagrams, not code |
| Geometry choice and why it separates solvers (R3) | High | An engineering object (not an arbitrary shape) whose features are tied to solver behaviour, plus a **complexity ladder** from cube to full object |
| Thermal analysis correctness (R4) | High | Verification: manufactured solution with O(h²) convergence, reproduction of the v1 rod result in 3D, fin theory, energy balance |
| Evidence quality of the comparison (R2) | High | Fair protocol, mesh sweep over ~2 decades of DOF, log-log scaling plots with fitted slopes, setup vs solve split, memory, a conditioning sweep |
| Scenario guidance (R6) | Very high (the notice's "ultimately") | A **decision chart** backed by measured crossovers: "below N DOF use X; with many load cases use Y; with thin layers / high contrast use Z" |
| Pictures (R7) | High | 3D renders of geometry, mesh and temperature field; cut planes; sparsity / fill pictures; AMG aggregates on the mesh; convergence curves; scaling charts |
| Clarity, honesty, no code (R7) | Medium–high | Plain-language summaries next to the maths, limitations section, reproducibility appendix (machine, compiler, protocol) without source listings |

**What would score highest, in one sentence:** a report that answers "which solver is best?" with
"it depends on these three measurable properties of the design, and here is where each crossover
lies on this object", proved by verified, reproducible, well-drawn experiments.

### 1.4 Constraints carried over from v1

- C++17, MSVC 14.44, CMake 3.31 (Build Tools path), no new runtime dependencies unless approved.
- Deterministic, reproducible outputs (v1 practice: fixed seeds, golden files, one-command regeneration).
- Honest AI-use log (`UniForge/docs/ai_usage_log.md`) and effort log continue.
- Commits only as `ftaakash`, no co-author/AI lines, phase-by-phase history, no push without approval.
- CI (Windows MSVC, Linux ASan/TSan) must stay green.

---

## 2. Geometry: candidates and choice

### 2.1 What a geometry must do to separate solvers

| Property of the design | Why it matters to solvers |
|---|---|
| **Many DOF for a given accuracy** (fine features force small elements) | Direct-solver memory/time grow super-linearly; iterative solvers grow ~linearly per iteration |
| **Thin parts / high element aspect ratio** (fins, thin layers) | Worsens conditioning; slows simple Krylov preconditioners; challenges AMG coarsening |
| **Material contrast** (e.g. aluminium next to a thermal interface material) | Condition number scales with k_max/k_min; Jacobi-PCG iterations rise |
| **Weak coupling to the ambient** (convection only, low Biot number, no fixed temperature) | Near-singular K (constant temperature almost costs nothing): smallest eigenvalue → 0, iterations rise; direct solvers do not care |
| **Shape of the domain** (plate-like fins vs compact block) | Sets separator sizes for nested dissection: thin fins behave more 2D, which keeps direct-solver fill lower [EST] |
| **Multiple load cases** (e.g. several power levels) | Direct factorisation is reused, iterative solves are repeated |

### 2.2 Candidates

| # | Geometry | Separates solvers via | Meshable from scratch with hexes | Analytical checks | Engineering relevance | Visual | v1 continuity |
|---|---|---|---|---|---|---|---|
| G1 | **Finned heat sink on a heated die** (base plate + straight fins + silicon die + thin TIM layer) | DOF, thin fins, material contrast (TIM), low Biot (convection only), plate-like domains | Yes (union of boxes on a non-uniform tensor grid) | Fin theory, energy balance, MMS on sub-boxes | Very high (electronics cooling, the textbook 3D conduction-convection case) | Very good | **v1 already ships `data/geometry/heatsink_fins.stl`** (viewer only, never simulated) |
| G2 | Pin-fin heat sink (array of square pins) | Same as G1, more surface and more slender parts | Yes | Pin-fin theory | High | Very good | Variant of G1 |
| G3 | L-bracket with a hole | Re-entrant corner singularity (local error), modest DOF | Hole needs tets or stair-casing | Weak (no closed form) | Medium | Medium | None |
| G4 | Block with internal cavity / cold plate with rectangular channels | Internal convection surfaces, contrast | Yes (rectangular channels) | Limited | High | Good (needs cut views) | Weak |
| G5 | Arbitrary imported STL, voxelised | Anything | Yes but stair-stepped boundaries distort convection area | None | Depends | Depends | Uses v1 importer |

### 2.3 Recommendation: G1, finned heat sink on a heated die, with a complexity ladder

**Reference design ("HS-7")**, dimensions taken from v1's `heatsink_fins.stl` so the object the
evaluator already saw in v1 becomes the object that is now analysed:

| Part | Size (mm) | Material (typical k, W/(m·K)) [VERIFY handbook values] | Role |
|---|---|---|---|
| Base plate | 60 × 40 × 4 | Aluminium alloy, k ≈ 200 | Spreads heat |
| 7 straight fins | 2 thick × 40 long × 26 tall, pitch 9 | Same aluminium, fused with the base (one conforming solid, unlike the v1 STL's separate shells) | Convect heat away |
| Thermal interface layer (TIM) | 20 × 20 × t_TIM (t_TIM = 0.1–0.5) | k ≈ 1–5 | **Conditioning stressor**: low k, thin |
| Silicon die | 20 × 20 × 0.5 (under the base, centred) | k ≈ 150 | Heat source: uniform volumetric generation q, as in v1 |

**Load case LC1 ("same analysis as before" in 3D):** steady conduction, uniform volumetric
generation q in the die (total power P is a design input, e.g. 20–50 W, chosen not claimed),
convection h·(T − T∞) on all exposed heat-sink surfaces (T∞ = 25 °C, h swept 5 … 500 W/(m²·K)
from natural to forced air [VERIFY typical ranges]), insulated die bottom and base underside
(mounted on a board). No fixed temperature at all: the problem is closed only by convection,
which is exactly the near-singular case that separates solvers.

**Load case LC0 ("the cube"):** the v1 bar in 3D (1 m × 10 mm × 10 mm, k = 10, q = 1e4, T0 = 100 °C,
TL = 20 °C, insulated sides). The 3D solution must reproduce v1's parabola (peak 188.2 °C at
x = 0.42 m) on the bar's centre-line. This is the bridge between v1 and v2. **LC0c** (A11): a
compact cube of the same volume (≈ 46.4 mm edge), same k and q, Dirichlet on two opposite faces,
insulated elsewhere: the "simple cube" in the literal sense.

Glossary used everywhere (naming fix): **v1 bar** = ladder level **L0a** = load case **LC0**;
**cube** = **L0b** = **LC0c**. The report says once that the notice calls the v1 object "the cube".

**Complexity ladder (for experiment E4, "design complexity vs solver"):**

| Level | Object | What it adds |
|---|---|---|
| L0a | v1 bar | Slender (100:1), quasi-1D, single material, Dirichlet ends |
| L0b | Compact cube (A11) | Compact 3D, single material, Dirichlet faces |
| L1 | Base plate only, heated die, convection | 3D spreading, Robin-only (near-singular) |
| L2 | HS-7: plate + 7 fins | Thin plate-like fins, large surface |
| L3 | HS-7 + TIM + die (multi-material) | Material contrast, thin anisotropic layer |
| L4 | Pin-fin variant (G2), e.g. 8 × 6 pins [EST layout] | Many slender parts, highest surface/volume |

**Why G1 separates the solvers (hypotheses to test, §5):**
- DOF grows fast with refinement because fins and TIM need several elements across their thickness.
- The TIM layer (k contrast ≈ 40–200 [EST], aspect ratio up to ~10 at coarse levels) and the
  convection-only closure (low Biot) raise the condition number, which hurts Jacobi-PCG most,
  IC(0)-PCG less, multigrid least, direct not at all.
- Fins are plate-like, so nested-dissection separators are small and the direct solver may stay
  competitive longer than on a compact cube of the same DOF [EST]. If true, that is a nice,
  non-obvious "geometry decides the solver" finding; if false, the report says so.

---

## 3. Solver triples

### 3.1 Candidate triples

| Triple | Solver A | Solver B | Solver C | Verdict |
|---|---|---|---|---|
| T1 | Dense/banded Gaussian elimination | Gauss–Seidel | SOR | Weak: B and C are the same family; SOR is a straw man; nothing modern |
| T2 | Sparse direct Cholesky (fill-reducing ordering) | Jacobi-PCG | Gauss–Seidel / SOR | Acceptable but C loses everywhere, so the scenario story is thin |
| **T3 (recommended)** | **Sparse direct Cholesky** (RCM and nested-dissection orderings) | **Preconditioned conjugate gradient** (Jacobi; IC(0) as variant) | **Algebraic multigrid** (smoothed aggregation; standalone V-cycle, plus AMG-preconditioned CG as variant) | One solver per family, each with a regime where it wins; matches the solver menu of typical commercial FE codes [VERIFY with vendor docs before saying so] |
| T4 | Steady direct | Transient explicit (forward Euler) | Transient implicit (backward Euler) | Different axis (time integration, not linear solver). The notice asks for the same (steady) analysis. Kept as optional extension §6.6 |

**Why T3:** the three families fail in different ways, so the report can tie each to a design
property: direct = memory/fill (problem size and shape), Krylov = conditioning (contrast, thin
layers, Biot), multigrid = setup cost and coarsening quality (anisotropy). That is precisely the
"design complexity vs solver selection" trade-off the notice asks for.

### 3.2 Build approach: from scratch vs library

| Option | Licence | Pros | Cons |
|---|---|---|---|
| **All three from scratch in C++17 (recommended)** | Repo MIT | Fully explainable "how I built it" (R5); no download; consistent optimisation level across solvers (fair comparison); fits v1's style | More work; my direct solver will be slower than industrial supernodal ones (state this as a limitation) |
| Eigen 3.4 (header-only) as **test oracle / reference point only** | MPL-2.0 [VERIFY] (file-level copyleft; fine to vendor unmodified with licence text) | Independent check of results; shows how my simplicial Cholesky compares to a mature one (SimplicialLDLT + AMD) and to Eigen's ConjugateGradient | ~3–4 MB download (needs your OK); must not become the "solver I built" |
| SuiteSparse (CHOLMOD), PETSc/hypre, AMGCL | LGPL / BSD / MIT [VERIFY] | Industrial strength | Heavy builds, licences to manage, and they would replace the work the notice asks me to justify |

Recommendation: **from scratch**, algorithms taken from textbooks and written in my own code (no
copying from LGPL code such as CSparse). Eigen optional, only in tests/experiments as an external
reference, decision D3 in §9.

**Fixed labels** (naming fix, used in every table, legend and file name): `DIRECT-ND`,
`DIRECT-RCM`, `PCG-J`, `PCG-IC0`, `AMG`, `AMG-PCG`.

### 3.3 Shared infrastructure (written once, used by all three)

- `CsrMatrix` (row pointers, column indices, values; symmetric full storage for SpMV, lower
  triangle extracted for Cholesky), COO → CSR assembly with duplicate summation, deterministic.
- Vector kernels (dot, axpy, norms) with **deterministic** blocked parallel reductions on v1's
  `WorkerPool` (fixed chunking so serial and parallel runs give bit-identical results, as in v1 Task D).
- A common `SolveReport`: setup time, solve time, iterations, residual history, final true
  residual ‖f − K·T‖/‖f‖ recomputed independently of the solver, **energy imbalance**
  1ᵀr = Σ(heat in) − Σ(heat out) in watts (A3), error history ‖T_k − T_ref‖∞ when a reference is
  given (A2), work in SpMV-equivalents (A9), bytes allocated by the solver,
  status (converged / max-iterations / stagnation / breakdown / over-budget / time-cap), plus
  breakdown details (e.g. IC(0) shift used) (A12).
- Interface: `solve(const CsrMatrix&, const Vector& f, Vector& T, const SolverOptions&) -> Result<SolveReport>`
  plus a separate `setup()` so setup and solve can be timed and reused (multiple load cases).

### 3.4 Solver A: sparse direct Cholesky

| Aspect | Plan |
|---|---|
| Algorithm | K = L·Lᵀ (or L·D·Lᵀ, decision during P4) after symmetric permutation P·K·Pᵀ. Steps: ordering → elimination tree → column counts / symbolic factorisation → numeric up-looking (row-by-row) Cholesky → forward/back substitution |
| Orderings | (1) natural, for the fill picture only; (2) **Reverse Cuthill–McKee** (envelope/band reduction, simple); (3) **geometric nested dissection** on the structured hex grid (recursive bisection by coordinate planes, separator = a grid plane; possible because our mesher knows the grid) |
| Data structures | CSR of the permuted lower triangle; L in compressed-column form sized exactly by the symbolic phase (no reallocations); elimination-tree parent array |
| Complexity (regular 3D grid, theory) | RCM/banded: memory O(n^{5/3}), flops O(n^{7/3}); nested dissection: memory O(n^{4/3}), flops O(n²) [VERIFY: George 1973; Davis 2006]. Solve after factorisation: O(nnz(L)) |
| Expected behaviour | Fastest at small n; **cost** independent of conditioning (contrast, Biot), but forward error grows ~κ·ε (still expected far below discretisation error; measured, A3) (I8); factorisation reusable for many load cases; hits a memory wall first, expected around 2–5 × 10⁵ DOF in 8 GB [EST]. Simplicial (no supernodes/BLAS-3), so slower than industrial direct solvers by a large factor [EST] |
| Safety / failure modes (A12) | Symbolic phase predicts nnz(L) **before** the numeric phase; if it exceeds the memory budget (8 GB, D10) the run is recorded as "over budget" instead of thrashing, plotted as ✕. Non-positive pivot → typed error naming the pivot index (singular or indefinite K). Index types: 64-bit column pointers for L |
| Verification | vs dense Gaussian elimination oracle (v1 `tests/support/DenseGauss.hpp`) on small meshes; P·K·Pᵀ = L·Lᵀ to round-off on random SPD and FE matrices; identical solution for all orderings; vs Eigen if D3 = yes |
| Pictures | Spy plots of K and L under natural / RCM / ND (fill visibly different); elimination-tree sketch; nnz(L) vs n log-log |

### 3.5 Solver B: preconditioned conjugate gradient

| Aspect | Plan |
|---|---|
| Algorithm | Standard PCG for SPD systems; residual history kept. The residual is **not** trusted alone (I2): on the near-singular convection-only problem the mean temperature converges last, so runs go to a tight tolerance while the error against a reference and the energy imbalance are recorded (A2, A3) |
| Conditioning estimate (A3) | κ(M⁻¹K) estimated from the Lanczos tridiagonal built from the CG coefficients (free by-product); plotted as iterations vs √κ_est |
| Preconditioners | **Jacobi** (diagonal scaling; trivially parallel, cannot break down) as the main variant; **IC(0)** (incomplete Cholesky with the sparsity of K) as the stronger serial variant. Breakdown is **likely**, not rare (I6): Q1 hexes with aspect ratio > √2 give positive off-diagonals [VERIFY], so K is not an M-matrix in the TIM. On a non-positive pivot: retry with diagonal shift α = 1e-3, 1e-2, … [VERIFY: Manteuffel 1980], record α and retries, report breakdown as an E3 finding (A7) |
| Failure modes (A12) | pᵀKp ≤ 0 (not SPD) → typed error; stagnation (no 10× residual drop within N iterations) → status; max iterations / time cap → status, plotted as ✕ |
| Complexity | Per iteration O(nnz(K)) ≈ 27·n for trilinear hexes. Iterations ∝ √κ; κ ∝ h⁻² so iterations ∝ h⁻¹ ∝ n^{1/3} in 3D [VERIFY: Saad 2003]; total ≈ O(n^{4/3}). Memory O(n) (K + 4–5 vectors; IC(0) adds one copy of the lower triangle) |
| Expected behaviour | Lowest memory; easy to multithread (SpMV, dot); iteration count grows with refinement, with material contrast, with element aspect ratio and as Biot → 0 (near-singular) |
| Verification | Converges to the direct solution (‖T_B − T_A‖∞ below tolerance-derived bound); CG properties: residuals A-orthogonality spot check on small cases; iteration count on a 1D/2D model problem matches the √κ estimate within a factor |
| Pictures | Residual vs iteration (log) for Jacobi vs IC(0); iterations vs DOF; iterations vs contrast and vs Biot |

### 3.6 Solver C: algebraic multigrid (smoothed aggregation)

| Aspect | Plan |
|---|---|
| Why AMG, not geometric MG | Thin TIM and 2 mm fins cannot be represented on a coarse geometric grid; AMG coarsens from the matrix, so it works on any mesh of any design. That generality is part of the "complex geometry" argument |
| Algorithm | Setup: strength-of-connection filter → greedy aggregation → tentative prolongator (piecewise constant per aggregate, which carries the near-null-space vector, the constant temperature) → prolongator smoothing (one damped-Jacobi step) → Galerkin coarse operator Kc = Pᵀ·K·P (sparse matrix–matrix product) → repeat to a coarse size of a few hundred → coarse solve with Solver A. Cycle: V(1,1) with symmetric Gauss–Seidel (or damped Jacobi / Chebyshev for the parallel variant) [VERIFY: Vaněk, Mandel, Brezina 1996] |
| Modes | (1) **standalone** V-cycle iteration (the "multigrid solver"); (2) **AMG-PCG**: one V-cycle as CG preconditioner (usually the best practical choice [EST]) |
| Symmetry requirement (A15) | For AMG-PCG the V-cycle must be an SPD operator: forward Gauss–Seidel pre-smoothing, **backward** Gauss–Seidel post-smoothing, equal sweep counts, P and R = Pᵀ, exact (direct) coarse solve. Test: xᵀM⁻¹y = yᵀM⁻¹x to round-off on random vectors, and xᵀM⁻¹x > 0 |
| Failure modes (A12) | Residual growth per cycle (factor > 1) → status "diverging"; coarse factorisation failure → typed error; hierarchy stats always written so a poor coarsening is visible |
| Complexity | Ideally iterations independent of n, work and memory O(n) with operator complexity Σ nnz(K_ℓ)/nnz(K) ≈ 1.2–2 [EST]; setup cost several solves' worth [EST] |
| Expected behaviour | Wins at large n; robust to refinement and contrast if aggregation follows strong connections; setup cost makes it lose at small n; anisotropy (thin TIM) is its known weak spot, which the strength threshold addresses |
| Verification | Two-grid and V-cycle convergence factor measured and bounded (< 0.5 target [EST]); Galerkin product checked against a dense Pᵀ K P on small cases; Kc symmetric and positive definite; constant vector preserved by P; same answer as Solver A |
| Pictures | **Aggregates coloured on the heat-sink surface** (a striking, explanatory picture); hierarchy table (levels, rows, nnz, operator complexity); convergence factor per cycle |

### 3.7 How the three are compared fairly

- Same matrix, same right-hand side, same machine, same compiler flags, single thread for the main
  comparison (the direct solver is serial), multithreaded results reported separately.
- **Single-thread runs pinned to one P-core** (A8): the i5-14600K has 6 P + 8 E cores and a serial
  run landing on an E-core would distort the ranking. The core used is recorded in the metadata.
- **Accuracy target = time-to-accuracy** (A2). Per mesh a reference T_ref is computed: the direct
  solution where it fits, otherwise AMG-PCG to relative residual 1e-12, cross-checked against
  direct on all sizes where both run. Every iterative solver runs to a tight tolerance while
  ‖T_k − T_ref‖∞ is recorded each iteration (O(n), cheap). Reported: time and iterations to reach
  (a) 1 % of the estimated discretisation error (Richardson estimate of T_max from neighbouring
  meshes) and (b) 1e-3 °C, plus the plain relative-residual-1e-10 numbers for completeness.
  Plots show error vs time, not only residual vs iteration.
- **Energy imbalance per solve** (A3): 1ᵀr equals the global heat imbalance in watts; a solve that
  stopped with the mean temperature wrong shows it here even when the relative residual looks small.
- **Work units** (A9): each iteration's cost in SpMV-equivalents and bytes moved per solve, so CG
  steps and V-cycles are comparable.
- Setup and solve timed separately, so "one load case" and "k load cases" scenarios both follow
  from the same runs.
- Timing protocol from v1 Task D: warm-up, idle-machine check, machine and build info recorded
  (Intel Core i5-14600K, 32 GB, Windows 11, MSVC 19.44 Release as in v1; re-read from
  `SystemInfo` at run time). **Repetitions by run time** (A6): 5 if < 10 s, 3 if < 60 s, 1 above
  (noise is small relative to such times; stated in the report). Median, min, max reported.
- **Per-run caps** (A6, D10): 30 min wall time, 10 000 iterations, 8 GB solver memory. A capped run
  is plotted as a hollow ✕ at that size with its reason, never silently dropped.
- Memory: (a) exact bytes held by each solver's data structures (accounted in code) and (b) peak
  process working set from a **fresh process per run** (the harness spawns one process per run,
  V2-7) (I15). Assembly memory and time are reported separately from the solvers' (A4).
- Limitation stated in the report: all three are my implementations at a similar optimisation
  level; absolute times would differ for industrial codes, the **scaling slopes and crossovers**
  are the transferable result.

---

## 4. 3D FEA formulation

### 4.1 Strong and weak form

```text
−∇·(k ∇T) = q                     in Ω         (q = 0 outside the die)
T = T_D                           on Γ_D        (LC0 only)
−k ∂T/∂n = h (T − T∞)             on Γ_R        (convection, LC1)
−k ∂T/∂n = 0                      on Γ_N        (insulated)

Weak form: find T with T = T_D on Γ_D such that for all w (w = 0 on Γ_D)
∫_Ω k ∇w·∇T dV + ∫_{Γ_R} h w T dA = ∫_Ω w q dV + ∫_{Γ_R} h T∞ w dA
```

k is constant per element (per material), so multi-material is handled by element data.

### 4.2 Element choice: trilinear hexahedra (Q1, 8 nodes)

| Option | Pros | Cons | Decision |
|---|---|---|---|
| **Q1 hexahedra on a non-uniform tensor grid with a cell mask** | Heat sink is a union of boxes, so hexes fit it exactly; conforming mesh for free; trivial refinement (split every cell r×r×r); grid knowledge enables geometric nested dissection; 2×2×2 Gauss is exact for the box-shaped elements | Only box-like geometry; no curved surfaces | **Recommended** |
| Linear tetrahedra (4 nodes) | Any geometry | Needs a tet mesher (TetGen AGPL / Gmsh GPL [VERIFY]), or splitting hexes into 6 Kuhn tets | Optional cross-check: split each hex into 6 tets (conforming on a tensor grid) to show the result does not depend on element type |

Element matrices (per hex, isoparametric, 2×2×2 Gauss):
`Ke = ∫ k Bᵀ B dV`, `fe = ∫ q N dV`; per convective face (bilinear quad, 2×2 Gauss):
`He = ∫ h Nᵀ N dA`, `ge = ∫ h T∞ N dA`. For axis-aligned boxes the Jacobian is diagonal and
constant, which keeps the code short and exact.

### 4.3 Mesh generation (from scratch)

1. Geometry = list of boxes with a material ID (die, TIM, base, fins) + a list of boundary-condition
   tags per box face.
2. Collect all box faces' x, y, z coordinates as **mandatory grid lines**. Between consecutive
   mandatory lines place n = ceil(gap/s) **equal** cells (no sliver cells), and grade so that
   neighbouring cell sizes differ by at most a factor 1.5 [EST] (A5). The only mesh parameter is
   the target spacing s (no integer refinement factor). The TIM gets a **fixed** number of cells
   through its thickness (1 or 2, an E3 variable), so the refinement sweep changes size, not the
   TIM's conditioning (A5). Max and mean aspect ratio are recorded per mesh.
3. Cell (i, j, k) is active if its centre lies in a box; it takes that box's material.
4. Nodes = corners of active cells, numbered compactly; boundary faces = active-cell faces whose
   neighbour is inactive (or outside), tagged by the BC of the box face they lie on.
5. Checks (tests): total volume exact; convective area exact; every boundary face tagged; the
   surface is watertight (each boundary edge shared by exactly two boundary faces); node count
   formulas on a single box.
6. Export: the boundary surface as triangles (for rendering and an STL via v1's `StlWriter`).

**Geometric mesh ladder (A1):** s_k = s₀ · 2^(−k/3), so the DOF roughly **doubles** per step:
s = 1.0, 0.79, 0.63, 0.50, 0.40, 0.31, 0.25, 0.20 mm. This gives ≥ 4 direct-solver points before
its memory wall and 8+ points for the iterative solvers over two decades; slopes are fitted by
least squares on log–log axes. Sizes **[EST, to be computed by the mesher]** (HS-7 volume
2.416e-5 m³ from v1's STL; thin fins give ~1.4–1.6 nodes per cell):

| s (mm) | Cells [EST] | DOF [EST] | CSR bytes ≈ 328·n [EST] |
|---|---:|---:|---:|
| 1.0 | 2.5e4 | 3.5e4 | 11 MB |
| 0.5 | 2.0e5 | 2.4e5 | 79 MB |
| 0.25 | 1.6e6 | 1.7e6 | 0.56 GB |
| 0.2 | 3.0e6 | 3.2e6 | 1.05 GB |

### 4.4 Assembly and boundary conditions (reusing v1)

- **Assembly straight into CSR** (A4): the sparsity pattern comes from grid connectivity (each
  node's ≤ 27 neighbours are known from the tensor grid), K is allocated once, and element
  matrices are scattered through a per-element local → CSR index map. Peak assembly memory ≈ the
  final K. (A COO triplet list would need ~64 × 16 B per hex, about 3 GB plus a sort buffer at
  3 × 10⁶ cells [EST], and would set the peak memory instead of the solvers.) Deterministic order;
  parallel assembly (colouring) only if assembly time matters in the plots.
- **Dirichlet (LC0):** v1's symmetric elimination generalised to CSR: move known columns to the
  right-hand side, identity rows, keep symmetry; save unconstrained rows to compute **reactions**
  (v1 `DirichletBC.cpp` logic, now on a sparse matrix).
- **Convection:** adds face mass terms to K and h·T∞ terms to f; no elimination needed.
- **Energy balance** (v1's strongest bug detector, kept as a pass/fail check):
  `∫ q dV = Σ convected heat + Σ Dirichlet reactions`, to round-off.

### 4.5 Verification ladder (each one is a figure or table in the report)

| Check | Expected | What it proves |
|---|---|---|
| Patch test: trilinear T on a distorted-spacing grid | Reproduced to round-off | Element and assembly are correct |
| **LC0 (cube) reproduces v1**: insulated bar, Dirichlet ends | Centre-line peak 188.2 °C at x = 0.42 m; nodal values match v1's 1D solution [EST: expected exact at nodes as in 1D, to be confirmed] | Continuity with v1; Dirichlet + source in 3D |
| Manufactured solution, smooth T on a box with Robin faces | L2 error O(h²), H1 O(h) on log-log | Convection BC and source terms |
| Single fin vs 1D fin theory (θ/θ_b = cosh m(L−x)/cosh mL, convective-tip correction) [VERIFY: Incropera] | Close when Biot ≪ 1, deviates as Biot grows (explained) | Physics on the actual fin geometry |
| Energy balance on every run | Residual ~1e-12 relative for converged direct solves [EST]; for iterative solves it measures how converged the mean temperature is (A3) | Global conservation, BC bookkeeping, solver quality |
| Cross-solver agreement | ‖T_A − T_B‖∞, ‖T_A − T_C‖∞ below the tolerance-derived bound | The three solvers solve the same system |
| Hex vs Kuhn-tet discretisation (optional) | Same field within discretisation error | Element-type independence |

---

## 5. Experiment design

### 5.1 Hypotheses (stated in the report before the data, then confirmed or refuted)

| ID | Hypothesis [EST, all of them] |
|---|---|
| H1 | All three solvers give the same temperatures on both simple objects. On the compact cube (L0b) their times are close; on the slender v1 bar (L0a) direct wins clearly because the problem is quasi-1D (RCM bandwidth = one cross-section, while CG needs iterations ∝ L/h) (A11) |
| H2 | Direct (ND) wins below a crossover DOF n₁; AMG-PCG wins above it; n₁ is located with ≥ 4 direct points (A1). Jacobi-PCG never wins on time single-threaded but uses least memory |
| H3 | PCG iterations grow ~n^{1/3} under refinement; AMG iterations stay ~flat |
| H4 | Lowering the Biot number (h → small), raising the TIM contrast and thinning the TIM raise κ_est (A3); PCG iterations grow ~√κ_est, AMG iterations rise little; direct **cost** is unchanged and its forward error grows with κ but stays far below the discretisation error |
| H5 | With k load cases, direct's crossover n₁ moves up (factorisation amortised) |
| H6 | Plate-like fins keep nested-dissection fill below that of a compact block with the same DOF |
| H7 | With all cores, Jacobi-PCG closes much of the gap to serial AMG (SpMV is bandwidth-bound, so speedup is limited [EST]) |
| H8 | IC(0) breaks down without a shift once the TIM aspect ratio passes a threshold (positive off-diagonals beyond about √2 [VERIFY]); Jacobi never breaks down (A7) |
| H9 | On a deliberately singular case (all faces insulated, no Dirichlet) every solver detects and reports the problem instead of returning temperatures (A12) |

### 5.2 Experiments

| ID | Experiment | Variables | Fixed | Answers |
|---|---|---|---|---|
| E0 | Simple-object baseline (A11) | 3 solvers (+ variants) × {v1 bar L0a, compact cube L0b} | LC0 / LC0c, a few meshes | H1, motivates the new geometry (R3) |
| E1 | Verification | spacing s | §4.5 cases | Correctness (R4) |
| E2 | **Mesh-refinement sweep** on HS-7 (L3) | geometric ladder s = 1.0 … 0.20 mm (A1), until each solver hits its cap | LC1, h = 25 W/(m²·K) [EST mid value], TIM cells fixed (A5) | H2, H3, scaling slopes, crossover n₁ |
| E3 | **Conditioning sweep** | (a) Biot via h = 5 … 500; (b) TIM k = 0.5 … 200 (200 = no contrast); (c) TIM thickness 0.1 … 0.5 mm with 1 or 2 cells through it (A5) | one mid mesh, ~2–3 × 10⁵ DOF (A6) | H4, H8; κ_est per case (A3) |
| E4 | **Complexity ladder** L0a → L4 | object | similar DOF per level (adjust s) and one fixed s | R6: same DOF, different design → different winner? H6 |
| E5 | Multiple load cases | k = 1, 5, 20 right-hand sides (power levels / ambient temps) | E2 meshes | H5 |
| E6 | Threads (secondary) | 1 … 20 threads, with and without E-cores (A8) | PCG-Jacobi, AMG with Jacobi smoother | H7, link to v1 Task D |
| E7 | Ordering study (direct) | natural / RCM / ND | small–mid meshes | Why the ordering is part of "how I built it" |
| E8 | **Failure modes** (A12) | singular case (all faces insulated); IC(0) without shift on thin TIM; a run over the memory cap | small meshes | H8, H9; failure-mode table in the report |

**Machine-time budget (A6):** official runs as **one overnight batch, target ≤ 8 h [EST]**, after
a ~1 h dry run at the two smallest sizes of every experiment. Caps from §3.7 bound every run.
Order: E1, E0, E7, E8 (short) → E3 (mid mesh) → E5 → E2 large sizes last, so a cap or crash late
in the night loses the least.

### 5.3 Metrics (every run, written to CSV + meta JSON like v1)

DOF, nnz(K); mesh spacing s, max/mean aspect ratio (A5); assembly time and bytes (A4); setup
time, solve time, total; iterations and SpMV-equivalents (A9); final true relative residual;
energy imbalance in W (A3); κ_est for PCG runs (A3); error history vs T_ref and time-to-accuracy
at both targets (A2); discretisation error proxy (max temperature and thermal resistance
R_th = (T_max − T∞)/P vs s with Richardson extrapolation); solver bytes and peak working set;
nnz(L) and flops estimate (direct); IC(0) shift and retries (A7); operator and grid complexity
(AMG); status and failure reason (A12); core used and repetitions (A6, A8); machine/build metadata.

Engineering outputs (the "thermal analysis" itself): peak die temperature, thermal resistance
R_th, heat split per fin, fin efficiency vs fin theory, temperature field.

### 5.4 Pictures and plots the PDF needs (pictorial requirement R7)

| # | Picture | How produced |
|---|---|---|
| P1 | HS-7 geometry render (isometric, materials coloured) + exploded view with labels | Renderer (§6, decision D5) + annotations |
| P2 | Boundary-condition schematic (die heat source, convective faces, insulated faces) | Hand-built SVG diagram |
| P3 | Mesh at s = 1.0 and s = 0.5 mm (wireframe), detail of the TIM layer | Renderer |
| P4 | Temperature field on the surface (colormap + legend in °C), two views | Renderer |
| P5 | Cut planes through die and a fin; fin temperature profile vs fin theory | Renderer + SVG plot |
| P6 | v1 bar (L0a) result: 3D bar coloured + centre-line vs v1 parabola | Renderer + SVG plot |
| P7 | MMS convergence log-log (slope 2) | SVG plot (v1 `SvgPlot`) |
| P8 | Spy plots: K, L(natural), L(RCM), L(ND) | **Density raster PNG** (entries binned per pixel, A14): L has 10⁷–10⁸ entries, too many for vector points |
| P9 | AMG aggregates coloured on the surface; hierarchy table | Renderer + table |
| P10 | Residual vs iteration for PCG-Jacobi, PCG-IC(0), AMG, AMG-PCG | SVG plot |
| P11 | Time vs DOF (log-log) with fitted slopes, setup/solve split | SVG plot |
| P12 | Memory vs DOF (log-log), direct's budget wall marked | SVG plot |
| P13 | Iterations vs DOF; iterations vs Biot; iterations vs contrast | SVG plots |
| P14 | Stacked bars: setup vs solve for 1 and k load cases | SVG plot |
| P15 | Complexity ladder: thumbnails L0–L4 with winner and time ratio per level | Composite figure |
| P16 | **Decision chart** "which solver when" (flowchart) backed by measured crossovers | Hand-built SVG |
| P17 | Software architecture of the solver framework (modules, shared interface) | Hand-built SVG |
| P18 | Algorithm flow diagrams for each solver (instead of code) | Hand-built SVG |
| P19 | Error vs time (time-to-accuracy) per solver, both targets marked (A2) | SVG plot |
| P20 | Iterations vs √κ_est across the E3 cases (A3) | SVG plot |
| P21 | Failure-mode table + E8 outcomes (A12) | Table + small plot |
| P22 | E0: bar vs cube, time per solver side by side (A11) | SVG plot |

**Figure tokens (A13)**, fixed in `docs/report_v2/figure_tokens.md` before V2-8 and used by every figure:

| Token | Value |
|---|---|
| Solver colours (colour-blind safe, Okabe–Ito [VERIFY hex values]) | direct `#0072B2` blue, PCG `#E69F00` orange, AMG `#009E73` green; theory/reference lines grey dashed |
| Variant encoding within a family colour | ND solid ●, RCM dashed ○; Jacobi solid ●, IC(0) dashed ■; AMG solid ▲, AMG-PCG dashed △ |
| Temperature colormap | inferno (as in v1), legend in °C with min/max ticks, same range across views of one case |
| Axes | log–log for every scaling plot, decades labelled 10ⁿ, fitted slope printed by each line, x axis always "DOF" |
| Failed / capped runs | hollow ✕ at the attempted size with a short reason label |
| Typography | Inter (text), JetBrains Mono (numbers in tables), units in every header (v1 report fonts) |
| Caption template | what is shown · case and mesh · what to notice · data file name |

### 5.5 Report outline (single PDF, no source code)

No page limit is stated; target **18–25 pages** [decision D6], A4, HTML → PDF via headless Edge
(v1 `tools/build_report.ps1`, page cap removed or raised).

1. Summary (1 page): question, object, three solvers, the answer + decision chart thumbnail.
2. From the cube to a complex object: why linear problems give identical answers; E0 result,
   bar vs cube (P22).
3. The object and the thermal problem: HS-7, materials, BCs, physics (P1–P2).
4. 3D finite-element formulation and mesh (P3), verification ladder (P6, P7, fin theory, energy).
5. Thermal results: temperature field, cut planes, R_th, fin efficiency (P4, P5).
6. How each solver was built: A, B, C (algorithm diagrams P18, data structures, complexity, tests,
   design choices and alternatives rejected; P8, P9).
7. Comparison: protocol; E2 scaling (P11–P13, P19); E3 conditioning (P20); E4 complexity ladder
   (P15); E5 load cases (P14); E6 threads; E8 failure modes (P21).
8. Which solver for which scenario: decision chart (P16) + table of scenarios with evidence.
9. Limitations and threats to validity; what I would do next (supernodal direct, parallel AMG,
   transient, nonlinear k(T)).
10. Appendix: machine and build, timing protocol, full result tables, AI-use statement, references.

"No source code" is read strictly: no code listings, not even pseudo-code in code style.
Algorithms are shown as numbered steps, equations and flow diagrams. [decision D7]

---

## 6. Phase plan

### 6.1 Where the code goes

- Branch `v2` from `main` (v1.0.0 stays untouched); tags `v2-phase-N`.
- New core modules, no GUI dependency:
  - `src/core/sparse/`: CSR, vector kernels, deterministic parallel reductions, Matrix Market writer (debug).
  - `src/core/fem3d/`: box geometry, tensor-grid hex mesher, Q1 elements, assembly, BCs, post-processing.
  - `src/core/solvers/`: `direct/` (orderings, symbolic, numeric Cholesky), `pcg/`, `amg/`, shared `SolveReport`.
  - `src/core/viz/` (if D5 = software renderer): CPU rasteriser → PNG, spy plots.
- CLI: `uniforge_cli solvers <experiment>` writing `results/v2/...` CSV/JSON/SVG/PNG, one command
  regenerates everything (`uniforge_cli v2-all`), as in v1.
- Report sources: `docs/report_v2/` (HTML, figures) → `UniForge_V2_Report.pdf`.
- GUI: no new view required by the notice. Optional "Solver Lab" view for a workbench screenshot
  in the report (decision D8).

### 6.2 Phases, commits, effort

Effort in hours is an **estimate** [EST]. Calibration: v1 took 13.7 h actual against 46 h planned
(v1 effort log), so the "planned" column follows the original sizing and the "reviewed" column
applies the design review's re-estimate. **Machine time, not coding, is now the expected
bottleneck** (A6).

Phase order changed (A10): the simple PCG-J is built **before** the FEA verification, so MMS and
fin-theory checks run on meshes up to ~10⁵ DOF instead of only what the dense oracle can handle.
PCG-J is then tested twice, as the verification workhorse and as Solver B.

| Phase | Content | Commits (examples, under `ftaakash`) | Exit criteria | Planned [EST] | Reviewed [EST] |
|---|---|---|---|---:|---:|
| V2-0 | Design freeze: this plan + review + further design-skill outputs merged into `docs/design/UniForge_V2_Build_Spec.md`; figure tokens (A13); AI-log entry | `v2-phase0(docs): v2 analysis, design and build spec` | You approve the spec | 2 h | 1–2 h |
| V2-1 | Sparse infrastructure: CSR, pattern-first assembly API (A4), deterministic parallel vector kernels | `v2-phase1(sparse): csr matrix and pattern-first assembly`, `…: deterministic parallel vector kernels` | Unit tests, ASan/TSan green | 5 h | 2–3 h |
| V2-2 | Geometry + graded tensor-grid hex mesher (A5) + surface export; bar, cube, HS-7, ladder objects | `v2-phase2(mesh): box geometry and graded tensor-grid hex mesher`, `…: boundary faces and surface export` | Volume/area/watertight tests; no neighbour ratio > 1.5; HS-7 surface STL viewable in v1 GeometryView | 6 h | 2–3 h |
| V2-3 | **Solver B core: PCG-J** with SolveReport (energy imbalance, error history, κ_est, work units, failure statuses) | `v2-phase3(pcg): jacobi pcg with solve report` | Converges on model problems; serial = parallel bit-for-bit; κ_est matches known κ of a 1D model problem within a factor | 3 h | 1.5–2 h |
| V2-4 | 3D FEA (Q1, Dirichlet, convection, direct-to-CSR assembly, energy balance) + verification E1 using PCG-J and the dense oracle | `v2-phase4(fem3d): q1 hex element and csr assembly`, `…: dirichlet and convection`, `…: verification (bar = v1, mms, fin)` | Patch test, bar reproduces 188.2 °C, MMS slope 2, fin theory, energy balance | 8 h | 3–4 h |
| V2-5 | Solver A: RCM, geometric ND, symbolic (fill prediction, budget check), numeric Cholesky | `v2-phase5(direct): rcm and nested dissection`, `…: symbolic and numeric cholesky` | vs dense oracle; all orderings agree; fill measured; non-positive pivot reported | 8 h | 3–5 h |
| V2-6 | PCG-IC(0) with shift-and-retry (A7) | `v2-phase6(pcg): ic0 preconditioner with shift retry` | Agrees with A; breakdown detected and recorded | 2 h | 1 h |
| V2-7 | Solver C: SA-AMG, symmetric V-cycle (A15), AMG-PCG | `v2-phase7(amg): aggregation and prolongator`, `…: galerkin product and symmetric v-cycle`, `…: amg as preconditioner` | Agrees with A; convergence factor bound; preconditioner symmetry test; hierarchy checks | 10 h | 4–6 h |
| V2-8 | Experiment harness: process per run, P-core pinning (A8), caps and repetition policy (A6), time-to-accuracy (A2), E0–E8; dry run, then **official overnight batch** | `v2-phase8(cli): solver experiments and sweeps`, `v2-phase8(results): official runs on an idle machine` | All CSVs regenerate with one command; golden smoke test in CI on small meshes; no run silently missing | 6 h | 6–8 h + **6–10 h machine time** |
| V2-9 | Figures: plots, renders, density spy plots (A14), aggregates, diagrams, all with tokens (A13) | `v2-phase9(viz): …`, `v2-phase9(results): report figures` | Every figure in §5.4 exists and is traceable to a CSV | 7 h | 6–8 h |
| V2-10 | Report PDF, AI-use log, effort log, polish, final checks | `v2-phase10(report): v2 report`, `v2-phase10(docs): ai-use and effort logs` | PDF builds, no code, every number traced, [EST]/[VERIFY] all resolved | 8 h | 6–8 h |
| | **Total** | | | **~64 h** | **~35–50 h + one overnight run** |

Optional (only if time allows, each separately approved): Eigen cross-check (D3, ~2 h), Kuhn-tet
cross-check (~2 h), Solver Lab GUI view (~4 h), transient explicit vs implicit (T4, ~5 h),
nonlinear k(T) with Picard iterations (~4 h).

### 6.3 Review gates (your workflow)

Each phase ends with: build + tests green locally (MSVC Release + Debug), summary of what changed,
then **your review before commit and before push**. Official timing runs only after you confirm the
machine is idle. The AI-use log gets an entry per working session, written during the session.

### 6.4 Risks

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| K1 | Direct solver memory explodes earlier than useful (too few points on the plot) | Medium | Medium | Symbolic phase predicts nnz(L) first; ND ordering; geometric ladder gives ≥ 4 direct points (A1); the wall itself is a result |
| K2 | AMG does not converge well on the thin TIM (anisotropy) | Medium | High | Strength threshold tuning; SGS smoother; AMG-PCG mode; time-boxed; fall back to reporting it honestly as AMG's weak spot (still a valid scenario finding) |
| K3 | IC(0) breaks down (non-positive pivot) on the thin TIM | **Medium–high** (positive off-diagonals at aspect ratio > √2 [VERIFY]) | Low | Shift-and-retry, recorded; reported as finding H8 (A7) |
| K4 | Results show no clear separation | Low | High | Conditioning knobs (Biot, contrast, thickness) and ladder L4 exist to force separation; E3 alone separates PCG from AMG |
| K5 | Timing noise / unfair comparison | Medium | High | v1 protocol, idle check, P-core pinning (A8), repetition policy (A6), same flags, setup/solve split, time-to-accuracy target (A2) |
| K5b | Iterative solver "converges" on residual but the mean temperature is wrong (near-singular, low Biot) | **High** without A2/A3 | High | Error vs reference recorded per iteration; energy imbalance per solve; κ_est (A2, A3) |
| K5c | Experiment campaign does not finish (machine time) | Medium | High | Caps, repetition policy, dry run, overnight batch with large sizes last (A6) |
| K5d | Assembly dominates peak memory | High with COO | Medium | Direct-to-CSR assembly; assembly reported separately (A4) |
| K6 | Rendering 3D fields for the PDF takes long (the v1 mesh renderer is flat-shaded, no per-vertex scalar) | Medium | Medium | D5: CPU software rasteriser in core (deterministic, CI-testable) or extend the D3D11 shader with per-vertex colour |
| K7 | Claims about commercial tools or textbook complexities unverified | Medium | Medium | Every such statement tagged [VERIFY] until checked; drop if not checkable |
| K8 | Scope creep (transient, nonlinear, GUI) | Medium | Medium | Optional items need separate approval |
| K9 | CI breaks on Linux/ASan with large tests | Low | Medium | CI uses tiny meshes; large runs only in the experiment command |
| K10 | Deadline unknown | ? | High | D1: ask; if short, cut L4, E6, E7 and optional items first |
| K11 | "No source code" interpreted differently by the evaluator | Low | Medium | No listings; diagrams and equations only (D7) |

---

## 7. Reuse map (v1 → v2)

| v1 piece | Reused as |
|---|---|
| `thermal/DirichletBC` symmetric elimination + reactions | Generalised to CSR (LC0) |
| Energy-balance and convergence-order checks (v1's fault-injection lesson) | Pass/fail checks in every v2 run |
| `tests/support/DenseGauss.hpp` | Oracle for direct solver and assembly on small meshes |
| `perf/WorkerPool`, `SystemInfo`, `Benchmark` protocol, `BenchmarkReportIO` | Threaded kernels, machine metadata, timing protocol, reports |
| `common/SvgPlot`, `CsvWriter`, `MetaJson`, `AtomicFile`, `Rng` | All plots and result files |
| `io/StlWriter`, `GeometryView`, `render/PngWriter` | Surface export, interactive check, PNG output |
| `data/geometry/heatsink_fins.stl` dimensions | HS-7 reference design |
| `tools/build_report.ps1` | PDF build (page cap changed) |

---

## 8. References to verify before use [VERIFY all]

- T. A. Davis, *Direct Methods for Sparse Linear Systems*, SIAM, 2006 (elimination tree, up-looking Cholesky).
- A. George, "Nested dissection of a regular finite element mesh", SIAM J. Numer. Anal., 1973.
- E. Cuthill, J. McKee, "Reducing the bandwidth of sparse symmetric matrices", 1969.
- Y. Saad, *Iterative Methods for Sparse Linear Systems*, 2nd ed., SIAM, 2003 (PCG, IC(0)).
- P. Vaněk, J. Mandel, M. Brezina, "Algebraic multigrid by smoothed aggregation for second and fourth order elliptic problems", Computing, 1996.
- W. L. Briggs, V. E. Henson, S. F. McCormick, *A Multigrid Tutorial*, 2nd ed., SIAM, 2000.
- F. P. Incropera et al., *Fundamentals of Heat and Mass Transfer* (fin equations, typical h ranges, material k).
- Vendor solver menus (e.g. ANSYS sparse/PCG) only if checked in vendor documentation; otherwise omitted.

Citation style: IEEE (as the routing table suggests); final list built with the citation skill.

---

## 9. Decisions for you

| ID | Decision | My recommendation |
|---|---|---|
| D1 | Deadline for the V2 reply? | Needed to size scope (cut list in K10) |
| D2 | Geometry | **G1 HS-7 finned heat sink on a die with TIM**, ladder L0–L4 |
| D3 | Download Eigen 3.4 (~3–4 MB, MPL-2.0) as an independent cross-check? | Optional; yes if time allows, never as one of the three solvers |
| D4 | Solver triple | **T3: sparse direct Cholesky / PCG (Jacobi, IC(0)) / smoothed-aggregation AMG** |
| D5 | 3D pictures: CPU software rasteriser in core (deterministic, testable, no GPU) vs extending the D3D11 renderer | CPU rasteriser |
| D6 | Report length | 18–25 pages |
| D7 | No code at all in the PDF, including pseudo-code | Yes, diagrams + equations only |
| D8 | Optional "Solver Lab" GUI view for a screenshot | Only if time remains after V2-10 |
| D9 | Repo: branch `v2` in the same repo (vs a new repo) | Same repo, branch `v2` |
| D10 | Per-run caps: 30 min wall time, 10 000 iterations, 8 GB solver memory (of 32 GB) | Accept (A6) |
| D11 | E0 with both the v1 bar and a compact cube, or the v1 bar only | Both (A11) |
| D12 | Overnight official batch: you confirm the machine stays idle and awake | Needed before V2-8 |

---

**Recorded 2026-11-18:** you asked to continue building without waiting for D1 and to take the
recommended default for every open decision. Adopted: D1 not given (build proceeds; cut list only
if a deadline needs it), D2 G1 HS-7, D3 no Eigen, D4 T3, D5 CPU rasteriser, D6 18–25 pages,
D7 yes, D8 no GUI view unless time remains, D9 branch `v2`, D10 the caps, D11 bar and cube,
D12 still needed before the overnight batch. The build follows `UniForge_V2_Build_Spec.md`.

---

## Appendix A: AI-use log entry (draft, to be added to `UniForge/docs/ai_usage_log.md` at V2-0 commit)

**2026-11-15: V2 requirements analysis and plan (Phase V2-0)**
- Tool: Claude Code (Claude desktop app, Code tab), model Claude Opus 5.5, started from a parent session.
- Prompt (summary): read the Assignment Extension Notice and the v1 repository; write an analysis and
  plan for V2 (requirements, geometry candidates, solver triples, 3D FEA formulation, experiment
  design, phase plan); do not build anything.
- Output: `UniForge_V2_Analysis_and_Plan.md` (this file).
- Verification so far: none of the [EST] numbers are measured; all [VERIFY] facts and citations are
  unchecked. The design-skill runs and your review come next.
- Questionable points to challenge in review: whether AMG from scratch fits the time budget (K2),
  whether the direct solver gives enough data points (K1), and whether "no pseudo-code" is too strict (D7).
- Review step (same day): `/design:design-system` audit of this plan →
  `UniForge_V2_Design_Review.md`. It found 16 issues in the AI-written plan, five rated high:
  integer refinement left the direct solver with two data points (I1); a residual-only stopping
  test is unsafe on the near-singular convection-only problem (I2); COO assembly would set peak
  memory (I3); no machine-time budget (I5); FEA verification was scheduled before any usable solver
  (I11). Also: the hypothesis "all solvers look alike on the cube" is probably false for the slender
  v1 bar (I13). You chose "fold them in"; amendments A1–A15 are now in this plan (revision 2).
  Still unverified: all [EST] numbers and [VERIFY] facts.
