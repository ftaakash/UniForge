# UniForge V2: Build Specification

**Project:** UniForge V2, reply to the PASS SCIENCE WORKS Assignment Extension Notice
**Repo:** `UniForge/` (`ftaakash/UniForge`), branch `v2` from `main`; v1.0.0 stays untouched
**Deliverable:** one PDF report, emailed as a reply. No source code in it; pictures essential.
**Status:** merged spec, **single source of truth for the V2 build.** Nothing is built yet.
Written 2026-11-15.

**Merged from (all 2026-11-15, all kept as design-iteration evidence):**
- `UniForge_V2_Analysis_and_Plan.md` rev 2 (**Plan**): requirements, geometry, solvers, FEA,
  experiments, phases; already contains review amendments A1–A15.
- `UniForge_V2_Design_Review.md` (**Review**, `/design:design-system`): 16 issues, amendments
  A1–A15, figure tokens.
- `UniForge_V2_System_Design.md` (**SysDesign**, `/engineering:system-design`): modules,
  interfaces, data models, harness, report pipeline; Design A vs B.

At the V2-0 commit the three sources and this spec move to `UniForge/docs/design/v2/`. §17 lists
what changed in the merge.

**Markers resolved in V2-10 (2026-11-23):** every estimate and unchecked fact below now states its outcome in
place: *(measured: …)* with the value and its result file, *(verified: …)* with the source checked, or *(not
verified, not used in the report)*. The report itself contains none.

Tags used while planning: **EST** = estimate, must be measured before it reaches the PDF. **VERIFY** = unchecked
fact or citation, must be checked or dropped before it reaches the PDF. Interface sketches are
design notation for this spec; none of it goes into the PDF.

---

## 0. Decisions

### 0.1 Settled

| # | Decision | Choice | Source |
|---|---|---|---|
| D-1 | Language / toolchain | C++17, MSVC 14.44 (VS 2022 Build Tools), CMake 3.31 at the Build Tools path; GCC 13 in Linux CI | v1 |
| D-2 | Dependencies | Core = standard library + Threads only; report tools = Python 3.14 stdlib + pypdf (installed) + Edge. **No downloads** unless you approve (Eigen = D-22) | v1, Plan |
| D-3 | Commits | Author and committer `ftaakash` only, **no Co-Authored-By or any AI attribution**, format `v2-phaseN(scope): summary`, annotated tag `v2-phase-N` per phase; the e-mail is whatever git is configured with and is never written into repo files | v1 rules |
| D-4 | Review gate | Build + tests green, summary to you, **your approval before every commit and every push** | v1 rules |
| D-5 | AI-use log | Honest entry per working session in `docs/ai_usage_log.md`, written during the session | v1 rules |
| D-6 | Review amendments | A1–A15 adopted (you chose "fold them in") | Review |
| D-7 | Report content | No code listings and no code-style pseudo-code; algorithms as numbered steps, equations and flow diagrams | Plan D7 |

### 0.2 Adopted defaults (you can still change these)

| # | Decision | Default | Alternative |
|---|---|---|---|
| D-8 | Geometry | **HS-7**: finned heat sink (v1 `heatsink_fins.stl` dimensions) on a silicon die with a thin TIM; ladder L0a bar, L0b cube, L1 plate, L2 fins, L3 + TIM/die, L4 pin fins | L-bracket, cold plate (Plan §2.2) |
| D-9 | Solver triple | **Sparse direct Cholesky / PCG / smoothed-aggregation AMG**, six variants `DIRECT-ND`, `DIRECT-RCM`, `PCG-J`, `PCG-IC0`, `AMG`, `AMG-PCG` | Direct / PCG / SOR |
| D-10 | Build approach | **All solvers from scratch** from textbook algorithms (no copying of LGPL code) | Library-backed |
| D-11 | Element | **Q1 hexahedra** on a graded tensor grid with a cell mask | Linear tets (Kuhn split, optional cross-check) |
| D-12 | Architecture | **SysDesign Design B** (persistent kernel pool, two-phase solver objects, observer subtraction, per-run processes, generated report numbers) | Design A (simpler, ~8–11 h less, weaker evidence) |
| D-13 | ND ordering | **Graph-based nested dissection** on the CSR graph; grid-plane ND as fallback if time is short | Grid-plane ND only |
| D-14 | 3D pictures | **CPU rasteriser in core** (`viz::Raster3D`); labels as SVG overlays | Extend the D3D11 shader |
| D-15 | `PngWriter` | **Move** from `app/render` to `core/viz` (behaviour unchanged) | Duplicate it |
| D-16 | Report length | **18–25 pages**, A4 (V2-10: 22 pages) | Shorter |
| D-17 | Run caps | **30 min wall, 10 000 iterations, 8 GB solver memory** per run | Other caps |
| D-18 | E0 objects | **v1 bar and a compact cube** | v1 bar only |
| D-19 | Repo layout | Same repo, **branch `v2`** | New repo |
| D-20 | Official batch | Refuses a dirty git tree | Allow |

### 0.2a Recorded 2026-11-18: defaults adopted, build started

You asked (through the planning session) to continue building V2 without waiting for the
deadline and to take the recommended defaults for every decision still open. Recorded:

| Decision | Adopted |
|---|---|
| D-8 … D-20 (§0.2) | All defaults as listed above |
| Plan D1 / spec D-21 (deadline) | **Not given; build proceeds.** The cut list (§16, K13) is applied only if a deadline arrives that needs it |
| Plan D3 / spec D-22 (Eigen download) | **No** (default: optional, not downloaded) |
| Plan D8 / spec D-23 (Solver Lab GUI view) | **Not built** unless time remains after V2-10 |
| Commits | **One commit per phase** on branch `v2` (replaces the example multi-commit lists in §13), annotated tag `v2-phase-N`; nothing goes to `main` before your review |

### 0.3 Needed from you

| Item | When |
|---|---|
| **D-21 Deadline for the V2 reply** (sets the cut list, §16) | Before V2-1 |
| D-22 Eigen 3.4 download (~3–4 MB, MPL-2.0; not verified, Eigen was not used) as an optional cross-check only | Before V2-5, optional |
| D-23 Optional "Solver Lab" GUI view for a screenshot | After V2-10, optional |
| Keep the machine idle and awake for the overnight official batch | V2-8 |
| Review and approve every phase before commit/push | Every phase |
| Send the PDF reply e-mail yourself | After V2-10 |

---

## 1. Requirements from the notice and how they are scored

### 1.1 Requirements

| ID | Requirement | Hard / soft |
|---|---|---|
| R1 | Apply **three different types** of solvers to the same thermal problem | Hard |
| R2 | Evaluate which is **most efficient / optimal** | Hard |
| R3 | If the simple geometry gives similar results because of linearity, **use a more complex 3D design** | Effectively hard |
| R4 | **Same thermal analysis as before** (steady conduction, heat generation) on a **different object** | Hard |
| R5 | **Justify how each solver was built** | Hard |
| R6 | **Which solver for which scenario**: design complexity vs solver selection | Hard, core judgement |
| R7 | **One PDF**, reply by e-mail, **no source code**, pictures essential, detailed | Hard |
| R8 | Only this single thermal operation needs comparing | Scope limit |

The notice calls the v1 object (1 m × 10 mm × 10 mm bar solved as a 1D rod) "the cube". The report
says so once and uses the glossary in §3.1.

### 1.2 The central argument

Steady conduction with constant k gives a linear SPD system K·T = f with a unique solution, so
**every correct solver returns the same temperatures** (that is verification, not a weakness).
Solvers differ in **cost and robustness**: time, memory, iterations, setup cost, sensitivity to
mesh size, aspect ratio, material contrast and boundary conditions. The geometry drives problem
size, conditioning and sparsity structure, and each solver family reacts to a different one.

### 1.3 Inferred judging criteria (no rubric given)

| Criterion | What scores highest |
|---|---|
| Three genuinely different, correct solvers | One per family (direct, Krylov, multigrid), each converging to the same verified answer |
| "How I built it" (R5) | Algorithm, data structures, design choices and rejected alternatives, complexity, tests, as diagrams |
| Geometry choice (R3) | An engineering object whose features map to solver behaviour, plus a complexity ladder |
| Thermal correctness (R4) | MMS O(h²), v1 result reproduced in 3D, fin theory, energy balance |
| Evidence quality (R2) | Fair protocol, two decades of DOF, fitted slopes, setup/solve split, memory, conditioning sweep, failures shown |
| Scenario guidance (R6) | A decision chart backed by measured crossovers |
| Pictures (R7) | Renders of geometry, mesh, temperature field, cut planes, fill patterns, AMG aggregates; consistent figure style |
| Clarity and honesty | Plain-language summaries, limitations, reproducibility appendix without code |

---

## 2. Non-functional requirements

| ID | Requirement | Target |
|---|---|---|
| N1 | **Fairness**: same `CsrMatrix`, same kernels, same flags, same timing code for all solvers | Hard |
| N2 | **Determinism**: bit-identical temperatures, iteration counts and residual histories for any thread count | Hard |
| N3 | **Memory**: assembly peak ≤ 1.2 × final K; no allocation inside iteration loops; footprint known before large allocations | Hard for direct (symbolic first) |
| N4 | **Instrumentation overhead** ≤ 2 % of solve time, or measured and subtracted | Measured in V2-8 |
| N5 | **Robustness**: every failure returns a typed status, never NaN temperatures or a hang; caps on time, iterations, memory | Hard |
| N6 | **Campaign resilience**: overnight batch survives a crash, a cap or a reboot and resumes | Hard |
| N7 | **Portability**: new core builds on MSVC and GCC 13; Linux ASan/TSan CI stays green | Hard |
| N8 | **Traceability**: every number and figure in the PDF maps to a result file and a run ID | Hard |

---

## 3. The thermal problem

### 3.1 Objects and glossary

| Name | Ladder | Load case | Description |
|---|---|---|---|
| **v1 bar** | L0a | LC0 | 1 m × 10 × 10 mm, k = 10 W/(m·K), q = 1e4 W/m³, T0 = 100 °C, TL = 20 °C, insulated sides; must reproduce v1's peak 188.2 °C at x = 0.42 m on the centre-line |
| **cube** | L0b | LC0c | Same volume (≈ 46.4 mm edge), same k and q, Dirichlet on two opposite faces, insulated elsewhere |
| plate | L1 | LC1 | HS-7 base plate + die, convection only |
| fins | L2 | LC1 | Plate + die + 7 fins, no TIM (clarified in V2-2: the die is the heat source at L1–L3; the TIM is added only at L3) |
| **HS-7** | L3 | LC1 | Full object below |
| pin fins | L4 | LC1 | Pin array variant, 8 × 6 square pins, 2 mm side (design input, `PinFinParams`) |

### 3.2 HS-7 reference design

| Part | Size (mm) | Material, k in W/(m·K) (design inputs of typical magnitude; not verified against a handbook table, and the report calls them chosen inputs) | Role |
|---|---|---|---|
| Base plate | 60 × 40 × 4 | Aluminium alloy, ≈ 200 | Spreads heat |
| 7 fins | 2 thick × 40 long × 26 tall, pitch 9, fused with the base | Same aluminium | Convect heat |
| TIM | 20 × 20 × t_TIM, t_TIM 0.1–0.5 | ≈ 1–5 | Conditioning stressor (low k, thin) |
| Silicon die | 20 × 20 × 0.5, under the base, centred | ≈ 150 | Uniform volumetric heat generation |

**LC1:** steady conduction; uniform q in the die (total power P a design input, e.g. 20–50 W,
chosen, not claimed); convection h·(T − T∞) on all exposed heat-sink surfaces, T∞ = 25 °C, h swept
5 … 500 W/(m²·K), base case 25 (design inputs; the range is not attributed to a source in the report); die bottom and base underside
insulated. No fixed temperature: closed by convection only (near-singular at low Biot number, the
case that separates solvers).

### 3.3 Formulation

```text
−∇·(k ∇T) = q                 in Ω   (q = 0 outside the die)
T = T_D                       on Γ_D (LC0, LC0c)
−k ∂T/∂n = h (T − T∞)         on Γ_R (LC1)
−k ∂T/∂n = 0                  on Γ_N
Weak form: ∫ k ∇w·∇T dV + ∫_{Γ_R} h w T dA = ∫ w q dV + ∫_{Γ_R} h T∞ w dA   for all w (w = 0 on Γ_D)
```

Q1 hex: `Ke = ∫ k Bᵀ B dV`, `fe = ∫ q N dV` with 2×2×2 Gauss (exact for boxes, diagonal constant
Jacobian); convective face (bilinear quad, 2×2 Gauss): `He = ∫ h Nᵀ N dA`, `ge = ∫ h T∞ N dA`.
k constant per element (material).

### 3.4 Mesh

1. Design = boxes with material and per-face BC tags; overlaps rejected.
2. All box-face coordinates are mandatory grid lines; between consecutive lines n = ceil(gap/s)
   **equal** cells (no slivers), graded so neighbouring sizes differ by ≤ 1.5 (design choice, enforced by the mesher; measured max grade recorded per mesh) (A5).
3. Only parameter: target spacing s. TIM gets a **fixed** 1 or 2 cells through its thickness (E3
   variable), so refinement changes size, not TIM conditioning (A5).
4. Cell active if its centre is inside a box; boundary faces = active faces with an inactive
   neighbour, tagged by the box face they lie on. Max/mean aspect ratio and max grade recorded.
5. Output: structured `HexMesh` (§6.1), boundary surface triangles (for `viz` and v1's `StlWriter`).

**Geometric ladder (A1):** s_k = 1.0 mm · 2^(−k/3) → s = 1.0, 0.79, 0.63, 0.50, 0.40, 0.31, 0.25,
0.20 mm; DOF roughly doubles per step; ≥ 4 direct points before its wall, 8+ iterative points.

**Measured** (V2-2 mesher, HS-7 with 1 TIM cell, grade ratio 1.5; meshing time ≤ 0.3 s per mesh).
The DOF are 16–60 % above the earlier estimate because grading next to the 0.1 mm TIM and the
fins adds cells; the step-to-step growth is 1.6–2.0×.

| s (mm) | Grid cells | Active cells | Nodes = DOF | Max / mean aspect | K bytes ≈ 328·n (measured: 325 B per DOF at 3.76 M DOF, E2.csv `matrix_bytes`) |
|---|---|---:|---:|---|---:|
| 1.000 | 70 × 40 × 38 | 44 080 | 56 015 | 10.0 / 2.50 | 18 MB |
| 0.794 | 83 × 52 × 45 | 75 192 | 93 921 | 7.94 / 1.88 | 31 MB |
| 0.630 | 107 × 64 × 55 | 141 504 | 170 634 | 6.25 / 1.63 | 56 MB |
| 0.500 | 127 × 80 × 66 | 241 600 | 285 532 | 5.00 / 1.39 | 94 MB |
| 0.397 | 162 × 103 × 82 | 496 764 | 568 152 | 3.92 / 1.29 | 0.19 GB |
| 0.315 | 203 × 128 × 101 | 912 384 | 1 022 979 | 3.13 / 1.18 | 0.34 GB |
| 0.250 | 247 × 160 × 125 | 1 696 800 | 1 867 768 | 2.50 / 1.10 | 0.61 GB |
| 0.198 | 317 × 203 × 157 | 3 484 283 | 3 759 720 | 1.98 / 1.10 | 1.23 GB |

The maximum aspect ratio is the TIM cell (in-plane s over 0.1 mm thickness), as intended.

### 3.5 Assembly and boundary conditions

- **Straight into CSR (A4):** pattern from grid connectivity (≤ 27 neighbours per node), K allocated
  once, element matrices scattered via a local → CSR index map. No COO (which would need ~3 GB +
  sort buffer at 3 × 10⁶ cells, an estimate; COO was never built, see AI log I3). Assembly time and bytes reported separately.
- **Measured (V2-4, HS-7, Release, one thread):** the matrix holds about 25.7 entries per row
  (≤ 27 for interior nodes).

  | s (mm) | DOF | nnz(K) | K bytes | Assembly time |
  |---|---:|---:|---:|---:|
  | 1.000 | 56 015 | 1 295 743 | 17.2 MB | 0.04 s |
  | 0.500 | 285 532 | 6 915 070 | 90.0 MB | 0.25 s |
  | 0.250 | 1 867 768 | 47 345 368 | 641.6 MB | 1.81 s |
  | 0.198 | 3 759 720 | 96 545 752 | 1164.4 MB | 4.67 s |

- **Dirichlet:** v1's symmetric elimination (`thermal/DirichletBC.cpp`) generalised to CSR; saved
  unconstrained rows give **reactions**.
- **Convection:** face terms added to K and f.
- **Energy audit** on every run: `∫q dV = convected heat + reactions` (W); additionally the solver's
  `1ᵀr` over free rows equals the heat imbalance and exposes a wrong mean temperature (A3).

### 3.6 Verification ladder

| Check | Expected | Proves |
|---|---|---|
| Patch test (trilinear T, graded grid) | Exact to round-off | Element + assembly |
| v1 bar (LC0) | Centre-line peak 188.2 °C at 0.42 m; nodal values = v1 1D (measured: largest nodal error 6.6e-11 K, E1.csv `E1-verify-bar`) | Continuity with v1 |
| Manufactured solution with Robin faces | L2 O(h²), H1 O(h) | Source and convection terms |
| Single fin vs 1D fin theory with convective tip (verified source: Bergman et al., *Fundamentals of Heat and Mass Transfer*, 7th ed., Wiley 2011) | Close for Biot ≪ 1, deviation explained | Physics on the real fin |
| Energy audit | ~1e-12 relative for direct solves (measured: largest relative imbalance of a converged direct run 6.6e-10, all_runs.csv) | Conservation, BC bookkeeping |
| Cross-solver agreement | ‖T_X − T_ref‖∞ below tolerance-derived bound | Same system solved |
| Hex vs Kuhn tets (optional) | Same field within discretisation error | Element independence |

---

## 4. Architecture (Design B)

### 4.1 Components

```text
uniforge_cli   study plan | batch | run | collect | figures | all   (v1 commands unchanged)
   platform/   Process (spawn, wait with deadline, kill) [v1], JobLimits (memory cap),
               Affinity (P-core pin), KeepAwake
      │
uniforge_core
   study/    RunSpec, Manifest, Runner (one run), Collect (CSV/JSON + numbers.json), Figures
   fem3d/    Design, HexMesh, Assembly, BCs, Post, EnergyAudit
   solvers/  direct/ (Ordering, Symbolic, NumericCholesky, TriangularSolve)
             krylov/ (Pcg, Jacobi, Ic0)
             amg/    (Strength, Aggregate, TentativeP, SmoothP, Galerkin, Hierarchy, VCycle)
   viz/      Raster3D, SurfaceField, Spy (density), Colormap, PngWriter (moved from app)
   sparse/   CsrMatrix, CsrPattern, KernelPool, vector kernels, SpGEMM, Transpose
   common/   (v1) Result, Error, CancelToken, ProgressFn, CsvWriter, MetaJson, SvgPlot, AtomicFile, Rng
   perf/     (v1) SystemInfo, Benchmark protocol pieces

tools/report_v2/  fill_numbers.py → report.html → Edge print → PDF → check_pdf.py
```

### 4.2 Dependency rules (CMake + CI lint)

| Module | May depend on | Must not depend on |
|---|---|---|
| `sparse` | `common` | all other new modules |
| `fem3d` | `sparse`, `common` | `solvers`, `viz`, `study` |
| `solvers` | `sparse`, `common` | **`fem3d`**, `viz`, `study` (lint: no `core/fem3d` include under `src/core/solvers/`) |
| `viz` | `common` | `fem3d`, `solvers` (takes plain arrays) |
| `study` | all core | `app`, OS headers |
| `cli/platform` | OS headers | – |

v1 rules unchanged: `uniforge_core` links only the standard library + Threads; OS code only in
`perf/SystemInfo_<os>.cpp` and `cli/platform`.

### 4.3 Repository layout (new parts)

```text
src/core/sparse/   src/core/fem3d/   src/core/solvers/{direct,krylov,amg}/   src/core/viz/   src/core/study/
src/cli/commands/StudyCommand.*      src/cli/platform/{JobLimits,Affinity,KeepAwake}_{win,posix}.cpp
tests/unit/{sparse,fem3d,solvers,viz,study}/   tests/golden/v2/
results/v2/{manifest.json,runs/,refs/,csv/,numbers.json,batch.log,progress.json}
docs/report_v2/{report.src.html,figure_tokens.md,fig/}   tools/report_v2/{fill_numbers.py,check_pdf.py,build_pdf.ps1}
docs/design/v2/   (this spec + the three sources)
```

### 4.4 Data flow of one run

```text
RunSpec ─► Design ─► HexMesh ─► LinearSystem{K, f, freeMask, energy terms}
                                     │      refs/<meshHash>.bin (T_ref) ─┐
                                     ▼                                   ▼
            Solver.setup(K, hint) ─► SetupReport ─► Solver.solve(f, x, control{observer}) ─► SolveReport
                                     │
                                     ▼
            Post: T_max, R_th, fin heats, EnergyAudit ─► RunResult.json (atomic) [+ field dump]
```

---

## 5. Solvers

### 5.1 Common interface and contract

```text
interface LinearOperatorSolver
    setup(K, hint: CoordinateHint?, pool: KernelPool, opts) -> Result<SetupReport>
    solve(f, x&, control: SolveControl)                     -> Result<SolveReport>
    footprint() -> MemoryFootprint
factory makeSolver(kind) -> unique_ptr<LinearOperatorSolver>
```

- `setup` once, `solve` per load case (factor / IC(0) / hierarchy reused, E5).
- **A failure is a status, not an `Error`.** `Result` carries an `Error` only for misuse
  (dimension mismatch, NaN input, I/O); `Breakdown`, `OverBudget` … are outcomes recorded and plotted.
- Every solver: no allocation in the iteration loop; NaN/Inf check on entry and exit.

### 5.2 Solver A: sparse direct Cholesky (`DIRECT-ND`, `DIRECT-RCM`)

| Aspect | Spec |
|---|---|
| Algorithm | P·K·Pᵀ = L·Lᵀ: ordering → elimination tree → column counts / symbolic → numeric up-looking Cholesky → forward/back substitution (verified source: Davis 2006, doi 10.1137/1.9780898718881) |
| Orderings | natural (fill picture only); **RCM** (verified source: Cuthill & McKee 1969, doi 10.1145/800195.805928); **graph ND** (level-structure bisection on the CSR graph, recursive, separators ordered last) (verified source: George 1973, doi 10.1137/0710032); grid-plane ND via `CoordinateHint` as fallback (D-13) |
| Data | Permuted lower triangle in CSR; L compressed-column, exactly sized by symbolic; **64-bit column pointers** for L |
| Budget | Symbolic predicts nnz(L) and bytes; over 8 GB → `OverBudget` before numeric allocation |
| Complexity (3D grid theory) | RCM: memory O(n^{5/3}), flops O(n^{7/3}); ND: memory O(n^{4/3}), flops O(n²) (theory not verified against a source and not stated in the report; measured fill slopes on HS-7: ND 1.30, RCM 1.53, from E2.csv `factor_nnz`) |
| Expected | Fastest at small n; cost independent of conditioning; forward error ~κ·ε (measured); wall ~2–5 × 10⁵ DOF in 8 GB (measured: RCM refused from 568 152 DOF, ND from 1 022 979 DOF, E2.csv); simplicial, so slower than industrial supernodal codes (not measured; stated as a limitation only) |
| Failures | Non-positive pivot → `Breakdown` + pivot index |
| Tests | vs dense oracle (v1 `tests/support/DenseGauss.hpp`); all orderings same solution; predicted nnz(L) = actual nnz(L) exactly; L·Lᵀ = P·K·Pᵀ on small cases |
| Pictures | Density spy plots K, L(natural/RCM/ND) (A14); nnz(L) vs n; elimination-tree sketch |

**Measured (V2-5, HS-7, Release, one thread, 8 GB budget; preliminary: not the official
protocol of §7, single run, machine not checked idle):**

| s (mm) | DOF | RCM nnz(L) | RCM setup | ND nnz(L) | ND setup |
|---|---:|---:|---:|---:|---:|
| 1.000 | 56 015 | 20 842 383 | 3.2 s | 16 585 484 | 4.7 s |
| 0.794 | 93 921 | 44 149 272 | 8.2 s | 31 021 545 | 11.6 s |
| 0.630 | 170 634 | 108 619 789 | 29.3 s | 69 330 210 | 38.7 s |
| 0.500 | 285 532 | 252 590 859 | 124.9 s | 130 836 333 | 94.9 s |
| 0.397 | 568 152 | OverBudget (≥ 707 M predicted, refused after 2.4 s) | – | 334 908 019 | 327.5 s |

The symbolic prediction equalled the actual nnz(L) in every run. ND always has the smaller
factor; it is slower than RCM up to 170 k DOF (its numeric phase runs at fewer flop/s), faster
from 285 k DOF. The direct wall in 8 GB is ≈ 0.57 M DOF for RCM; for ND it lies beyond
0.57 M DOF and will be located in the official runs (V2-8).

### 5.3 Solver B: preconditioned conjugate gradient (`PCG-J`, `PCG-IC0`)

| Aspect | Spec |
|---|---|
| Algorithm | Standard PCG for SPD (verified sources: Hestenes & Stiefel 1952, doi 10.6028/jres.049.044; Saad 2003, doi 10.1137/1.9780898718003); residual history and per-iteration timestamps |
| Stopping | Tight relative residual; **residual not trusted alone** (A2/A3): error vs reference via observer, `residualSum` (energy imbalance), κ_est |
| κ estimate | From the Lanczos tridiagonal built from CG coefficients (free) (A3) |
| Jacobi | Diagonal scaling; parallel; cannot break down |
| IC(0) | Sparsity of K; shift ladder α = 0, 1e-3, 1e-2, 1e-1, 1 on K + α·diag(K) (verified source: Manteuffel 1980, doi 10.1090/S0025-5718-1980-0559197-0); α and retries recorded (A7). **Corrected in V2-6:** the earlier "aspect ratio > √2" threshold holds for 2D bilinear elements only; for trilinear boxes the coupling along an edge is positive as soon as that edge is longer than the other two (element test `NonCubicBoxesHavePositiveEdgeCouplings`), so K has positive off-diagonals on practically every mesh (68 872 on HS-7 at 2 mm). Measured: IC(0) did **not** break down on the bar or on HS-7 at 2 mm (no shift needed); breakdown and the ladder are shown on Kershaw's 4 × 4 matrix (α = 1 after four failures). Whether thinner TIM meshes break it is an E3/E8 question |
| Complexity | Iteration O(nnz) ≈ 27n; iterations ∝ √κ, κ ∝ h⁻² → ∝ n^{1/3}; total O(n^{4/3}); memory O(n) |
| Failures | pᵀKp ≤ 0 → `Breakdown`; no 10× drop in N iterations → `Stagnated`; caps → `MaxIterations` / `TimeCap` |
| Tests | Converges to reference; model-problem iteration counts; κ_est vs analytic κ of a 1D Laplacian within a factor; serial = parallel bit-for-bit; shift ladder on a constructed non-M-matrix |
| Pictures | Residual and error vs iteration/time; iterations vs DOF, vs √κ_est, vs Biot, vs contrast |

### 5.4 Solver C: smoothed-aggregation AMG (`AMG`, `AMG-PCG`)

| Aspect | Spec |
|---|---|
| Why algebraic | Thin TIM and 2 mm fins cannot be represented on a coarse geometric grid; AMG coarsens from the matrix, so it works on any design |
| Setup | strength filter (θ) → greedy aggregation → tentative P (constant per aggregate = near-null-space) → one damped-Jacobi smoothing of P (ω from ρ(D⁻¹K) by power iteration) → Galerkin Kc = Pᵀ(K·P) via two SpGEMMs with exact-size symbolic pass → repeat to a few hundred rows → coarsest level factored by Solver A (verified source: Vaněk, Mandel & Brezina 1996, doi 10.1007/BF02238511) |
| Cycle | V(1,1); `SymGaussSeidel` (forward pre, **backward** post) serial, `L1Jacobi` for the threaded variant |
| Modes | `AMG` = V-cycle as stationary iteration; `AMG-PCG` = one V-cycle as CG preconditioner |
| Symmetry (A15) | Equal pre/post sweeps, R = Pᵀ, exact coarse solve; test xᵀM⁻¹y = yᵀM⁻¹x and xᵀM⁻¹x > 0 on random vectors |
| Complexity | Ideally flat iterations, O(n) work and memory, operator complexity 1.3–1.8 (measured with θ₀ = 0.02: 1.00–1.08 on all official runs); setup = several solves (measured: AMG-PCG setup 2.8 s vs solve 6.7 s at 3.76 M DOF, E2.csv) |
| Failures | Residual growth per cycle > 1 → `Diverging`; coarse factor failure → `Error`; hierarchy stats always written |
| Tests | P preserves constants; Kc symmetric positive definite; Galerkin vs dense Pᵀ K P on small cases; two-grid factor bound (< 0.5, asserted by `Amg.ConvergenceDoesNotDegradeWithMeshSize`; measured mean factor 0.28–0.31); same answer as reference |
| Pictures | **Aggregates coloured on the HS-7 surface**; hierarchy table; convergence factor per cycle |

**Measured (V2-7, preliminary, single runs):**
- **Superseded in V2-9.1:** θ₀ = 0.08 below was tuned on the 7-point Laplacian and is wrong for
  trilinear hexahedra (every coupling of a cubic cell is ≤ 1/16 of the diagonal, so nothing is
  aggregated); the default is now **θ₀ = 0.02** (halved per level as before) and the official AMG
  rows were re-run. Before/after: `docs/v2_experiments.md`.
- Strength threshold θ_l = 0.08·0.5^l (halving per level) is needed: with a constant θ the
  stand-alone cycle count on the 3D Laplacian grew 16 → 30 → 56 → 89 from 10³ to 32³; with the
  decay it is 15 → 16 → 16 → 19 (mean factor 0.28–0.36), operator complexity 1.5–1.6.
- PCG iterations on a 24³ Laplacian (to 1e-10): Jacobi 109, IC(0) 39, AMG 11.
- **HS-7 at 2 mm (13 200 DOF): the thin TIM is AMG's weak spot (risk K2 materialised).**
  Stand-alone AMG needs 673 cycles to 1e-11 (plate without fins and TIM: 56 cycles to 1e-10;
  fins without TIM: 62), and neither θ (0.08, 0.02, 0) nor a second smoothing sweep cures it
  (two sweeps: 263 cycles). As a preconditioner it stays the best Krylov option: AMG-PCG 84
  iterations vs PCG-IC0 117 and PCG-J 800 (to 1e-13). The report must show this honestly;
  remedies (material-aware strength, semi-coarsening across the TIM) are out of V2 scope.

### 5.5 Shared kernels (`sparse`)

- `KernelPool`: threads start once and park on a barrier (v1's `perf::WorkerPool` spawns threads
  per `run()` call, which is right for v1's batch, wrong for kernels called thousands of times).
- `parallelFor` / `reduce` over **fixed 4096-row chunks independent of thread count**; the caller
  sums the partial array in index order → bit-identical for p = 1 … 20 (N2).
- Kernels: `spmv`, `dot`, `axpy`, `xpay`, `normInf`, `jacobiApply`, `l1JacobiSweep`; sequential
  kernels (Gauss–Seidel, triangular solves, Cholesky) on the calling thread. SpGEMM and transpose
  live here so they are tested before AMG.

---

## 6. Data models

### 6.1 Geometry and mesh (`fem3d`)

| Type | Fields |
|---|---|
| `Material` | `id`, `name`, `k`, `q` |
| `FaceBc` | `Insulated` / `Convection{h, Tinf}` / `Dirichlet{T}` |
| `Box` | `min[3]`, `max[3]` (m), `materialId`, `faceBc[6]` |
| `Design` | `name`, boxes, materials, `ladderLevel`; factories `makeV1Bar`, `makeCube`, `makePlate`, `makeFins`, `makeHS7(params)`, `makePinFin(params)` |
| `MeshSpec` | `targetSpacing`, `maxGradeRatio` (1.5), `timCells` (1 or 2) |
| `GridAxis` | sorted `lines` |
| `HexMesh` | `axis[3]`, `cellMaterial` (uint8, 0xFF inactive), `nodeOfGridPoint` (int32, −1 unused), `nodeXyz`, `boundaryFaces{cell, side, bc}`, `stats{cells, nodes, aspect min/max/mean, max grade}` |
| `LinearSystem` | `K`, `f`, `freeMask`, `dirichletRows`, `heatIn`, convection terms |
| `CoordinateHint` | `nodeXyz` + `gridIndex` as plain arrays (no `fem3d` types cross into `solvers`) |
| `EnergyAudit` | `heatIn`, `heatConvected`, `reactions`, `imbalance` (W), `relImbalance` |

### 6.2 Sparse and solver reports

| Type | Fields |
|---|---|
| `CsrMatrix` | `n` int32, `rowPtr` int64, `col` int32 (sorted), `val` double; full symmetric storage, `lowerView()` |
| `CsrPattern` | `rowPtr`, `col` |
| `MemoryFootprint` | bytes per named component (`K`, `L`, `IC0`, `hierarchy[ℓ]`, `P[ℓ]`, `work`) from `capacity()·sizeof` |
| `SolverOptions` | ordering; IC(0) shift ladder; AMG θ, smoother, sweeps, coarse size, max levels |
| `SolveControl` | `relTol`, `maxIterations`, `wallTimeCap`, `memoryBudgetBytes`, `observer*`, `CancelToken*`, `ProgressFn` |
| `SetupReport` | `seconds`, `footprint`; direct: `predictedNnzL`, `nnzL`, `flops`; IC(0): `shift`, `retries`; AMG: `levels[]{rows, nnz}`, `operatorComplexity`, `gridComplexity` |
| `SolveReport` | `status ∈ {Converged, MaxIterations, Stagnated, Breakdown, OverBudget, TimeCap, Cancelled, Diverging}`, `failureDetail`, `iterations`, `workSpmvEq`, `seconds`, `observerSeconds`, `residualHistory`, `iterTimestamps`, `errorHistory?`, `kappaEstimate?`, `finalTrueRelResidual`, `residualSum` |

### 6.3 Runs (`study`)

| Type | Fields |
|---|---|
| `RunSpec` | `runId` (stable hash of all inputs), experiment, design + params, `MeshSpec`, solver + options, control, repetitions, reference policy, pinning, `dumpField` |
| `Manifest` | ordered RunSpecs, budget per experiment, build version |
| `RunResult` | RunSpec echo; mesh stats; assembly time/bytes; SetupReport; SolveReport per repetition (median/min/max); T_max, R_th, fin heats; EnergyAudit; peak working set; core used; load before/after; SystemInfo; git commit; timestamps; status incl. `Crashed` |
| `ReferenceStore` | `refs/<meshHash>.bin`: header (n, solver, tolerance, residual) + raw doubles |

---

## 7. Instrumentation and fair comparison

- Same matrix, RHS, machine, flags; **single thread for the main comparison**, threaded results separate.
- **Serial runs pinned to one P-core** (i5-14600K: 6 P + 8 E cores); core recorded (A8).
- **Observer** (`onIteration(k, residualNorm, x)`): each call bracketed by two clock reads;
  reported solve time = wall − observer time. Observers: `ResidualOnly`, `ErrorVsReference(T_ref)`,
  `Snapshot(every m)`. V2-8 check: with vs without `ErrorVsReference` on small sizes, subtracted
  times agree within 2 % (measured: 0.15 %, PCG-J at 171 k DOF, E2.csv) (N4).
- **Reference T_ref** per mesh: direct where it fits, else `AMG-PCG` to 1e-12, cross-checked against
  direct on all overlapping sizes (A2).
- **Time-to-accuracy** (A2): time and iterations to reach (a) 1 % of the Richardson-estimated
  discretisation error of T_max and (b) 1e-3 °C; plus plain 1e-10 relative residual numbers.
- **Energy imbalance** and **κ_est** in every result (A3); **work units** in SpMV-equivalents and
  bytes moved (A9).
- Setup and solve timed separately (one vs k load cases from the same runs).
- **Repetitions by run time** (A6): 5 if < 10 s, 3 if < 60 s, 1 above; median/min/max; warm-up;
  idle check (CPU load from `SystemInfo`) before each run.
- **Caps** (D-17): 30 min, 10 000 iterations, 8 GB solver memory; capped runs plotted as ✕.
- **Memory**: component footprints (exact) + peak working set of a fresh process per run.
- Limitation stated in the report: all solvers are mine at a similar optimisation level; the
  transferable results are **slopes and crossovers**, not absolute times.

---

## 8. Experiments

### 8.1 Hypotheses (stated before the data, then confirmed or refuted; outcomes in `docs/v2_experiments.md`)

| ID | Hypothesis |
|---|---|
| H1 | Same temperatures on both simple objects; on the cube times are close; on the slender bar direct wins clearly (quasi-1D) |
| H2 | Direct (ND) wins below crossover n₁, AMG-PCG above; n₁ located with ≥ 4 direct points; PCG-J never wins on time single-threaded but uses least memory |
| H3 | PCG iterations ∝ n^{1/3}; AMG iterations ~flat |
| H4 | Lower Biot, higher TIM contrast, thinner TIM raise κ_est; PCG iterations ∝ √κ_est; AMG rises little; direct cost unchanged, forward error grows but stays far below discretisation error |
| H5 | With k load cases n₁ moves up (factorisation amortised) |
| H6 | Plate-like fins keep ND fill below a compact block with the same DOF |
| H7 | All cores: PCG-J closes much of the gap to serial AMG; speedup bandwidth-limited |
| H8 | IC(0) breaks down without shift on sufficiently thin / stretched TIM cells; Jacobi never does. (V2-6: not observed on HS-7 at 2 mm, aspect ratio up to 20; to be tested on the full E3 sweep.) |
| H9 | On a singular case (all faces insulated) every solver detects and reports it |

### 8.2 Experiment matrix

| ID | Experiment | Variables | Fixed | Answers |
|---|---|---|---|---|
| E0 | Simple objects | all variants × {v1 bar, cube} | LC0 / LC0c, a few meshes | H1, R3 motivation |
| E1 | Verification | s | §3.6 cases | R4 |
| E2 | Refinement sweep on HS-7 | s = 1.0 … 0.20 (A1) until caps | LC1, h = 25, TIM cells fixed | H2, H3, slopes, n₁ |
| E3 | Conditioning sweep | h = 5 … 500; TIM k = 0.5 … 200; TIM 0.1 … 0.5 mm with 1 or 2 cells | one mesh ~2–3 × 10⁵ DOF | H4, H8 |
| E4 | Complexity ladder L0a → L4 | object | similar DOF per level; and one fixed s | R6, H6 |
| E5 | Load cases | k = 1, 5, 20 RHS | E2 meshes | H5 |
| E6 | Threads | 1 … 20, with/without E-cores | PCG-J, AMG (L1-Jacobi) | H7 |
| E7 | Orderings | natural / RCM / graph ND (+ grid ND if built) | small–mid meshes | R5 for direct |
| E8 | Failure modes | singular case; IC(0) without shift on thin TIM; run over memory cap | small meshes | H8, H9 |

### 8.3 Machine-time budget (A6)
**Dry run (V2-8, 85 runs, not the official protocol):** every run left a result (79
converged, 4 breakdowns and 2 setup breakdowns on the singular case E8, 2 stand-alone AMG
stagnations on HS-7). H9 failed for the direct solvers on the first try (a 2e13 °C answer on the
singular system) and holds after the fix in `DirectCholesky` (pivot ratio and residual checks).
Process-to-process timing noise was ~10 % (PCG-J at 94 k DOF: 1.468 vs 1.340 s, both medians of
5), so the observer-overhead check N4 needs the idle official batch. Solver time of the dry run
was ~7.5 min; the official batch was estimated at 4–7 h (measured: 2 h 46 min).


**Official batch (V2-8, 2026-11-23, commit `5e9bea7`):** 241/241 runs left a result in
2 h 46 min (205 converged, 23 stand-alone AMG stagnations, 7 direct runs over the 8 GB
budget, 6 singular runs detected). First reading of H1–H9 in `docs/v2_experiments.md`.
**AMG re-run (V2-9.1, 2026-11-23, commit `9be2090`):** after the strength-threshold fix the 72 AMG
and AMG-PCG rows were re-run in 11.5 min; the batch now stands at 228 converged, 0 stagnated,
7 over budget, 6 singular runs detected.

One **overnight batch, target ≤ 8 h (measured: 2 h 46 min)**, after a **~1 h dry run** at the two smallest sizes of
every experiment. Order: E1, E0, E7, E8 → E3 → E5 → E4 → E6 → E2 largest sizes last.

### 8.4 Metrics per run

DOF, nnz(K), s, aspect ratios; assembly time/bytes; setup, solve, total time; iterations, SpMV-eq;
true relative residual; energy imbalance (W); κ_est; error history and time-to-accuracy (both
targets); T_max, R_th = (T_max − T∞)/P, fin heats, Richardson estimate; solver bytes, peak working
set; nnz(L), flops; IC(0) shift/retries; operator/grid complexity; status and failure reason; core,
repetitions, load; machine/build/git metadata.

---

## 9. Harness

```text
uniforge_cli study plan    --experiments E0..E8 --out results/v2/manifest.json
uniforge_cli study batch   --manifest … [--dry-run] [--only E2] [--resume]
uniforge_cli study run     --manifest … --run-id <id>     (one run, own process)
uniforge_cli study collect --results results/v2/runs --out results/v2/csv   (+ numbers.json)
uniforge_cli study figures --csv results/v2/csv --out docs/report_v2/fig
uniforge_cli study all     (plan → batch → collect → figures; one-command regeneration, as v1)
```

`batch` (single-threaded orchestrator):
- skips RunSpecs that already have a `RunResult` (**resume**, N6);
- spawns `study run` in a **Windows Job Object with a hard memory limit** (budget + margin), pinned
  per policy; Linux CI uses the posix stub without limits (small meshes only);
- waits with a deadline (cap + margin) → kill → `TimeCap` result; crash → `Crashed` with exit code;
- `SetThreadExecutionState(ES_SYSTEM_REQUIRED)` while running; `progress.json` (done, total, ETA,
  current run) and `batch.log`;
- refuses a dirty git tree for official runs (D-20); each result records commit and flags.

---

## 10. Failure taxonomy (A12)

| Failure | Detected in | Outcome | Report |
|---|---|---|---|
| Predicted fill over budget | `direct::Symbolic` | `OverBudget`, no allocation | ✕ on plots |
| Cholesky pivot ≤ 0 | `direct::NumericCholesky` | `Breakdown` + index | E8 |
| IC(0) pivot ≤ 0 | `krylov::Ic0` | shift ladder, then `Breakdown` | E3/E8, H8 |
| pᵀKp ≤ 0 | `krylov::Pcg` | `Breakdown` | E8 |
| No 10× drop in N iterations | `Pcg`, `Amg` | `Stagnated` | E3 |
| Residual growth per cycle > 1 | `Amg` | `Diverging` | E3/E8 |
| Iteration / time cap | iterative solvers, orchestrator | `MaxIterations` / `TimeCap` | ✕ |
| OS memory limit / crash | orchestrator | `Crashed` + exit code | ✕ + log |
| NaN/Inf | every solver entry/exit | `Error{NonFinite}` | run fails |
| Converged residual, wrong mean | observer, `residualSum`, EnergyAudit | flagged in RunResult | E3 discussion |

---

## 11. Memory model at 3.2 × 10⁶ DOF (estimate; measured at 3.76 M DOF: K 1.22 GB, solver memory PCG-J 0.15 GB, IC(0) 0.78 GB, AMG 0.75 GB, peak process memory 1.66–3.00 GB, E2.csv `matrix_bytes`, `solver_bytes`, `peak_ws_bytes`)

| Component | Bytes |
|---|---:|
| Mesh (grid maps + coordinates) | ~0.12 GB |
| K (8.6 × 10⁷ nnz) | ~1.05 GB |
| PCG-J vectors | ~0.13 GB |
| IC(0) factor | ~0.55 GB |
| AMG hierarchy + P | ~0.6–1.1 GB |
| Reference + observer | ~0.05 GB |
| Direct | refused by symbolic check (far over 8 GB) |

Only the direct solver reaches the 8 GB budget, as intended.

---

## 12. Figures and the report

### 12.1 Figures

| # | Figure | Produced by |
|---|---|---|
| P1 | HS-7 render (materials) + exploded labelled view | `viz::Raster3D` + SVG overlay |
| P2 | BC schematic | hand-built SVG |
| P3 | Mesh at s = 1.0 and 0.5 mm, TIM detail | Raster3D |
| P4 | Surface temperature field, two views | Raster3D (inferno) |
| P5 | Cut planes through die and fin; fin profile vs theory | Raster3D + SvgPlot |
| P6 | v1 bar result: 3D bar + centre-line vs v1 parabola | Raster3D + SvgPlot |
| P7 | MMS convergence log–log | SvgPlot |
| P8 | Spy: K, L(natural/RCM/ND) as density rasters | `viz::Spy` |
| P9 | AMG aggregates on the surface + hierarchy table | Raster3D (categorical) |
| P23 (added V2-10) | E6 time vs threads, PCG-J and AMG-PCG, with ideal speed-up | SvgPlot |
| P24 (added V2-10) | AMG threshold before/after on the E2 sweep (from `results/v2_superseded`) | SvgPlot |
| P10 | Residual vs iteration, all iterative variants | SvgPlot |
| P11 | Time vs DOF with fitted slopes, setup/solve split | SvgPlot |
| P12 | Memory vs DOF, direct budget wall | SvgPlot |
| P13 | Iterations vs DOF / Biot / contrast | SvgPlot |
| P14 | Setup vs solve stacked bars, 1 and k load cases | SvgPlot |
| P15 | Complexity ladder thumbnails with winner and ratio | composite |
| P16 | **Decision chart "which solver when"** with measured crossovers | hand-built SVG |
| P17 | Software architecture | hand-built SVG |
| P18 | Algorithm flow diagrams per solver | hand-built SVG |
| P19 | Error vs time (time-to-accuracy) | SvgPlot |
| P20 | Iterations vs √κ_est | SvgPlot |
| P21 | Failure-mode table + E8 outcomes | table + plot |
| P22 | E0 bar vs cube | SvgPlot |

### 12.2 Figure tokens (A13, fixed in `docs/report_v2/figure_tokens.md` before V2-9)

| Token | Value |
|---|---|
| Family colours (Okabe–Ito; verified V2-10 against the source's Fig. 16: blue 0,114,178; orange 230,159,0; bluish green 0,158,115) | direct `#0072B2`, PCG `#E69F00`, AMG `#009E73`; theory/reference grey dashed |
| Variants | ND solid ●, RCM dashed ○; Jacobi solid ●, IC(0) dashed ■; AMG solid ▲, AMG-PCG dashed △ |
| Temperature colormap | inferno, legend in °C with min/max, same range across views of one case |
| Axes | log–log for scaling, decades as 10ⁿ, fitted slope printed, x axis "DOF" |
| Failed/capped | hollow ✕ with reason label |
| Typography | Inter, JetBrains Mono for numbers, units in every header |
| Caption | what is shown · case and mesh · what to notice · data file |

### 12.3 Report outline (18–25 pages; the V2-10 report has 22, with section 8 "Hypotheses" added
between the comparison and the decision chart)

1. Summary: question, object, three solvers, answer + decision chart thumbnail.
2. From the cube to a complex object: linearity → identical answers; E0 bar vs cube (P22).
3. The object and the problem: HS-7, materials, BCs (P1, P2).
4. 3D FEA and mesh (P3); verification ladder (P6, P7, fin theory, energy).
5. Thermal results: field, cut planes, R_th, fin efficiency (P4, P5).
6. How each solver was built: A, B, C (P18, P8, P9, P17), design choices and rejected alternatives.
7. Comparison: protocol; E2 (P11–P13, P19); E3 (P20); E4 (P15); E5 (P14); E6; E8 (P21).
8. Which solver for which scenario: decision chart (P16) + evidence table.
9. Limitations and next steps (supernodal direct, parallel AMG, transient, nonlinear k(T)).
10. Appendix: machine/build, protocol, full tables, AI-use statement, references (IEEE style).

### 12.4 Pipeline

```text
runs/*.json ─► study collect ─► csv/*.csv + numbers.json ─► fill_numbers.py ─► report.html
study figures ─► fig/*.svg|png (renders: PNG; labels: SVG overlays)          │
                                                     Edge headless print ─► UniForge_V2_Report.pdf
                                                                            │
                                                     check_pdf.py (pypdf): pages, size, text scan
```

- Every number in the prose is a `{{key}}` placeholder filled from `numbers.json`, with a
  provenance comment (run IDs) (N8).
- `check_pdf.py` fails on: unresolved placeholder, a leftover estimate or verify marker, code-like text
  (`#include`, `std::`, `for (` …), e-mail addresses, absolute paths, PDF > 15 MB, page count
  outside 18–25.
- v1's `PngWriter` writes uncompressed PNG (~6.4 MB per 1600 × 1000 render); whether Edge
  recompresses in the PDF was not checked; fix if needed: smaller renders or a real deflate encoder.
  **Done in V2-9:** `viz::PngWriter` now writes adaptive row filters and a fixed-Huffman
  deflate block (D-15 amended: moved *and* compressed; stored blocks kept as an option). The
  28 figure files are 2.7 MB in total, so the PDF size no longer depends on Edge.

---

## 13. Phase plan

Calibration: v1 took 13.7 h actual against 46 h planned. Machine time is the expected bottleneck.

| Phase | Content | Commits (examples) | Exit criteria | Effort (estimate; actual hours in `docs/effort_log.md`) |
|---|---|---|---|---:|
| V2-0 | Branch `v2`; this spec + sources into `docs/design/v2/`; figure tokens; AI-log entry | `v2-phase0(docs): v2 design documents and build spec` | You approve | 1–2 h |
| V2-1 | `sparse`: CSR, pattern, **KernelPool** with deterministic reductions, kernels, SpGEMM, transpose | `v2-phase1(sparse): csr matrix and pattern`, `…: persistent kernel pool with deterministic reductions`, `…: spgemm and transpose` | Invariant tests; p = 1/2/7/20 bit-identical; ASan/TSan green | 3–4 h |
| V2-2 | `fem3d` geometry + graded mesher + surface export + all ladder designs | `v2-phase2(mesh): box designs and graded tensor-grid mesher`, `…: boundary faces and surface export` | Volume/area exact; watertight; grade ≤ 1.5; HS-7 STL opens in v1 GeometryView | 2–3 h |
| V2-3 | Solver interface, `SolveReport`, observers, κ_est, **PCG-J** | `v2-phase3(solvers): solver interface, reports and observers`, `…: jacobi pcg` | Model problems; κ_est check; deterministic | 2–3 h |
| V2-4 | Q1 element, CSR assembly, Dirichlet, convection, EnergyAudit, post; **verification E1** with PCG-J + dense oracle | `v2-phase4(fem3d): q1 hex element and csr assembly`, `…: dirichlet and convection`, `…: verification (bar = v1, mms, fin)` | Patch test; bar 188.2 °C; MMS slope 2; fin theory; energy audit | 3–4 h |
| V2-5 | Direct: RCM, graph ND (grid ND fallback), symbolic + budget, numeric, solves | `v2-phase5(direct): rcm and nested dissection`, `…: symbolic and numeric cholesky` | Dense oracle; orderings agree; predicted = actual nnz(L) | 4–6 h |
| V2-6 | IC(0) with shift ladder | `v2-phase6(krylov): ic0 with shift ladder` | Breakdown detected and recorded | 1 h |
| V2-7 | AMG: strength, aggregation, P, Galerkin, symmetric V-cycle, AMG-PCG | `v2-phase7(amg): aggregation and prolongator`, `…: galerkin hierarchy and symmetric v-cycle`, `…: amg as preconditioner` | Symmetry test; hierarchy checks; factor bound; agrees with reference | 4–6 h |
| V2-8 | `study` + `cli/platform` (JobLimits, Affinity, KeepAwake); RunSpec/Manifest/Runner/Collect; ReferenceStore; observer-overhead check; golden tiny manifest in CI; **dry run, then official overnight batch** | `v2-phase8(study): run specs, manifest and runner`, `v2-phase8(cli): study batch with job limits and resume`, `v2-phase8(results): official runs on an idle machine` | Resume works after a kill; no run silently missing; overhead ≤ 2 % or subtracted | 7–9 h + **6–10 h machine** |
| V2-9 | `viz` (Raster3D, Spy, Colormap, PngWriter move); all figures P1–P22 with tokens | `v2-phase9(viz): cpu rasteriser and spy plots`, `v2-phase9(results): report figures` | Every figure exists and maps to a CSV/run | 7–9 h |
| V2-10 | Report HTML, `fill_numbers.py`, `check_pdf.py`, PDF; AI-use and effort logs; final checks | `v2-phase10(report): v2 report`, `v2-phase10(docs): ai-use and effort logs` | `check_pdf.py` passes; every estimate and verify marker resolved | 7–9 h |
| | **Total** | | | **~43–60 h + one overnight run** |

Optional, each separately approved: Eigen cross-check (D-22, ~2 h), Kuhn-tet cross-check (~2 h),
Solver Lab GUI view (D-23, ~4 h), transient explicit vs implicit (~5 h), nonlinear k(T) (~4 h).

---

## 14. Things only you can do

1. Tell me the deadline (D-21).
2. Approve each phase before commit and push.
3. Approve any download (Eigen, D-22).
4. Keep the machine idle and awake for the overnight batch (V2-8).
5. Read the final PDF and send the reply e-mail yourself.
6. Learn the solvers well enough to discuss them if PASS asks follow-up questions.

---

## 15. Traceability (phase exit = rows green)

| Req | Component | Tests | Evidence in PDF |
|---|---|---|---|
| R1 | `solvers/*` | per-solver tests (§5) | §6 of report, P18 |
| R2 | `study`, E2–E6 | golden tiny manifest | P11–P14, P19, P20 |
| R3 | designs L0a–L4, E0, E4 | mesher tests | P15, P22 |
| R4 | `fem3d`, E1 | patch, bar = v1, MMS, fin, energy | P4–P7 |
| R5 | `solvers/*`, E7 | orderings agree, symmetry, hierarchy | P8, P9, P17, P18 |
| R6 | all experiments | – | P16 decision chart + evidence table |
| R7 | report pipeline | `check_pdf.py` | the PDF itself |
| N1–N2 | `sparse`, KernelPool | bit-identical tests | appendix protocol |
| N3, N5 | symbolic budget, failure taxonomy, E8 | budget and failure tests | P12, P21 |
| N4 | observers | overhead check (V2-8) | appendix |
| N6 | orchestrator | resume-after-kill test | appendix |
| N7 | CI | Linux ASan/TSan | – |
| N8 | `numbers.json`, `fill_numbers.py` | placeholder check | provenance comments |

---

## 16. Risks

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| K1 | Direct hits its wall too early | Medium | Medium | Geometric ladder, ND, symbolic budget; the wall is a result |
| K2 | AMG weak on thin TIM | **Occurred (V2-7); main cause found and fixed (V2-9.1)** | High | Stand-alone AMG 673 cycles on HS-7 at 2 mm in V2-7. The official batch showed AMG weak on the plain cube too: θ₀ = 0.08 marked all Q1 couplings of cubic cells weak. With θ₀ = 0.02 all AMG runs converge and AMG-PCG is the fastest solver at every HS-7 size; stand-alone AMG still needs 365 cycles on HS-7 at 1 mm vs 15 on the cube (the TIM remains harder) |
| K3 | IC(0) breakdown | Medium (not seen at 2 mm) | Low | Shift ladder, recorded, finding H8 |
| K4 | No clear separation | Low | High | E3 knobs and L4 force separation |
| K5 | Timing noise / unfair comparison | Medium | High | Pinning, repetition policy, idle check, observer subtraction, time-to-accuracy |
| K6 | Iterative "converges" with wrong mean | High without A2/A3 | High | Error vs reference, energy imbalance, κ_est |
| K7 | Campaign does not finish | Medium | High | Caps, dry run, resume, large sizes last |
| K8 | Assembly dominates memory | Low (A4) | Medium | CSR assembly, reported separately |
| K9 | Rendering takes long / PDF too big | Medium | Medium | CPU rasteriser; size check in `check_pdf.py` |
| K10 | Unverified claims (vendor menus, complexities, material data) | Medium | Medium | Resolved V2-10: references verified, material data and complexity theory labelled as inputs / not stated in the report |
| K11 | Scope creep | Medium | Medium | Optional items need approval |
| K12 | CI breaks with big tests | Low | Medium | CI uses tiny meshes and a tiny manifest |
| K13 | Deadline unknown | ? | High | **Cut list in order:** E6 → L4 → graph ND (keep grid ND) → E5 at k = 20 → figures P15/P22 merged → report 18 pages |
| K14 | "No source code" read differently | Low | Medium | D-7 strict reading; `check_pdf.py` scan |
| K15 | Infrastructure bugs in Design B (more moving parts) | Medium | Medium | Tests in §5, §9, §15 |

---

## 17. Merge record

**From Plan (rev 2):** requirements R1–R8, central argument, judging criteria, geometry HS-7 and
ladder, solver triple and build approach, formulation, verification ladder, hypotheses, experiments
E0–E8, metrics, figures P1–P22, report outline, phase content, risks, references list (below).

**From Review:** amendments A1–A15 (already in Plan rev 2) and their placement here: A1 §3.4;
A2 §7; A3 §3.5, §5.3, §7; A4 §3.5; A5 §3.4; A6 §7, §8.3; A7 §5.3; A8 §7; A9 §7; A10 §13 order;
A11 §3.1, E0; A12 §10; A13 §12.2; A14 P8; A15 §5.4. Naming glossary, re-estimated effort.

**From SysDesign (Design B):** NFRs N1–N8, component diagram and dependency rules, data models,
two-phase solver interface with failure-as-status, persistent `KernelPool` with p-independent
reductions, observer subtraction, graph ND with grid hint, `study` harness with per-run processes,
Job Object limits, resume and keep-awake, report pipeline with generated numbers and PDF checks,
memory model, `PngWriter` move.

**Conflicts resolved in the merge:**
1. Plan's `solve(K, f, T, opts)` → SysDesign's `setup` / `solve` objects (§5.1).
2. Plan's grid-plane ND → graph ND default, grid ND as fallback and first cut-list item (D-13, K13).
3. Plan's "record error each iteration" → observer with subtracted time (§7).
4. Plan's `uniforge_cli solvers` / `v2-all` → `uniforge_cli study …` / `study all` (§9).
5. Plan's v1 `build_report.ps1` reuse → Python fill/check + Edge print step (§12.4).
6. Effort: Plan rev 2 reviewed 35–50 h + SysDesign +8–11 h → **43–60 h + overnight run** (§13).
7. Phase content regrouped: SpGEMM moved to V2-1; harness platform pieces to V2-8; `PngWriter` move to V2-9.
8. Plan's location `docs/design/UniForge_V2_Build_Spec.md` → `docs/design/v2/` with its sources (§4.3).

**New in the merge:** cut list order (K13), explicit settled/default/needed-from-you split (§0),
traceability table (§15), D-20 dirty-tree rule, PDF page-count check range.

### References (verified V2-10 against Crossref and Open Library; IEEE list in the report, Appendix C)

Davis 2006 (*Direct Methods for Sparse Linear Systems*, SIAM); George 1973 (nested dissection,
SIAM J. Numer. Anal.); Cuthill & McKee 1969; Saad 2003 (*Iterative Methods for Sparse Linear
Systems*, 2nd ed., SIAM); Manteuffel 1980 (shifted incomplete Cholesky); Vaněk, Mandel & Brezina
1996 (smoothed aggregation, Computing); Briggs, Henson & McCormick 2000 (*A Multigrid Tutorial*,
2nd ed., SIAM); Incropera et al. (*Fundamentals of Heat and Mass Transfer*). Vendor solver menus
only if checked in vendor documentation.

---

## Appendix A: AI-use log entries (draft, added to `docs/ai_usage_log.md` at V2-0)

**2026-11-15: V2 analysis, review, system design and merged spec (Phase V2-0)**
- Tool: Claude Code (Claude desktop app, Code tab), Claude Opus 5.5.
- Prompts (summary): (1) plan V2 from the extension notice and the v1 repo, do not build;
  (2) `/design:design-system`: analyse the plan, suggest amendments, evaluate cost and robustness;
  (3) "Fold them in"; (4) `/engineering:system-design` on revision 2; (5) "Merge into build spec".
- Outputs: Plan (rev 1, rev 2), Review, SysDesign, this spec.
- Problems found in AI output by the AI review passes: 16 issues in the plan (five high: two-point
  direct data, unsafe residual stopping on a near-singular problem, COO assembly memory, no
  machine-time budget, verification before a usable solver; plus the probably false "solvers look
  alike on the cube" hypothesis for the slender bar). The system-design pass found that v1's
  `WorkerPool` starts threads on every call, unsuitable for solver kernels.
- Verification so far: none at V2-0 (all estimates unmeasured, all facts unchecked); resolved in V2-10, see the note at the top.
- My edits / decisions: to be filled in by you at review (choices among D-8 … D-23, deadline).
