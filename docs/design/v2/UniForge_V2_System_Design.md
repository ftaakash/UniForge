# UniForge V2: System Design

> **Historical input, kept as written (note added in V2-10, 2026-11-23).** This document predates the
> build and was superseded by `UniForge_V2_Build_Spec.md`. The estimates and unchecked facts
> (tags EST and VERIFY below) that the build relied on were carried into the Build Spec and
> resolved there in V2-10 (measured with its result file, verified with its source, or marked
> not used in the report); the others were not used.
> Measured outcomes are in `docs/v2_experiments.md`; the report contains no unresolved marker.

Produced with `/engineering:system-design` from revision 2 of `UniForge_V2_Analysis_and_Plan.md`
and from `UniForge_V2_Design_Review.md` (2026-11-15). Scope: module boundaries, interfaces and data
models for the new core modules (`sparse`, `fem3d`, `solvers`, `viz`, `study`), the experiment
harness and the report pipeline. The design is evaluated on cost and robustness (time, memory,
iterations, mesh size, conditioning). §2 sets the design implied by the plan (**Design A**)
against an alternative (**Design B**); §10 compares them.

Tags as before: **[EST]** = estimate, to be measured; **[VERIFY]** = unchecked fact. Interface
sketches are C++-flavoured design notation for this document only; none of it goes into the PDF.

Facts about v1 that shape this design (read from the repo, not assumed):
- Core API never throws; every fallible call returns `Result<T>` with `Error{code, message, hint}`
  (`core/common/Result.hpp`, `Error.hpp`). Cancellation via `CancelToken`, progress via `ProgressFn`.
- `uniforge_core` links only the standard library (+ Threads); the only OS-specific code is
  `perf/SystemInfo_<os>.cpp`. CMake + CI lint enforce this (`Final_Build_Spec §4.2`).
- `perf::WorkerPool::run()` **starts and joins new `std::thread`s on every call** (fork-join per
  batch). That is right for v1's coarse batch, wrong for solver kernels called thousands of times (§6).
- `render::PngWriter` is plain C++ with stored (uncompressed) deflate, but lives in `src/app`.
- `src/cli/platform/Process_{win,posix}.cpp` already spawns child processes.

---

## 1. Requirements

### 1.1 Functional

| ID | Requirement | From plan |
|---|---|---|
| F1 | Describe box-union designs (bar, cube, plate, HS-7, pin fins) with materials and face BC tags | §2.3 |
| F2 | Mesh them into conforming Q1 hexes on a graded tensor grid at target spacing s | §4.3, A1, A5 |
| F3 | Assemble K, f straight into CSR; Dirichlet (symmetric elimination + reactions), convection, volumetric source | §4.4, A4 |
| F4 | Six solver variants: `DIRECT-ND`, `DIRECT-RCM`, `PCG-J`, `PCG-IC0`, `AMG`, `AMG-PCG` | §3 |
| F5 | Per solve: setup/solve time, iterations, work units, true residual, energy imbalance, κ_est, error history vs reference, bytes, status/failure reason | §3.3, A2, A3, A9, A12 |
| F6 | Experiments E0–E8 as a resumable batch with caps, repetitions, P-core pinning | §5, A6, A8 |
| F7 | Figures P1–P22: SVG plots, PNG renders (surface field, cut planes, aggregates), density spy plots | §5.4, A13, A14 |
| F8 | One PDF report built from data, with no hand-copied numbers and no code | §5.5, D7 |

### 1.2 Non-functional (cost and robustness are first-class here)

| ID | Requirement | Target [EST unless stated] |
|---|---|---|
| N1 | **Fairness**: all solvers see the same `CsrMatrix`, same kernels, same flags, same timing code | Hard |
| N2 | **Determinism**: same input → bit-identical temperatures, iteration counts and residual histories, **for any thread count** | Hard (v1 standard) |
| N3 | **Memory**: assembly peak ≤ 1.2 × final K; no solver allocates inside the iteration loop; footprint predictable before allocation | Hard for direct (symbolic first) |
| N4 | **Instrumentation overhead** ≤ 2 % of solve time, or measured and subtracted | Measured |
| N5 | **Robustness**: every failure mode returns a typed status, never NaN temperatures or a hang; caps on time, iterations, memory | Hard |
| N6 | **Campaign resilience**: an overnight batch survives a crash, a cap or a reboot and resumes without redoing finished runs | Hard |
| N7 | **Portability**: new core code builds on MSVC and GCC 13 (Linux ASan/TSan CI) | Hard (v1 standard) |
| N8 | **Traceability**: every number and figure in the PDF maps to a result file and a run ID | Hard |

### 1.3 Constraints and assumptions

- C++17, standard library only in core; Python 3.14 stdlib + pypdf for report tooling (installed);
  Edge for HTML → PDF. No new downloads (Eigen stays optional, D3).
- Machine: i5-14600K (6 P + 8 E cores, 20 threads), 32 GB RAM, Windows 11. Linux CI has fewer cores.
- Largest planned problem: ~3.2 × 10⁶ DOF [EST]. 32-bit column indices are enough
  (nnz(K) ≈ 8.6 × 10⁷ < 2³¹); nnz(L) for the direct solver may approach 10⁹, so its pointers are 64-bit.
- Effort: one developer with AI assistance, ~35–50 h + one overnight run (plan rev 2).

---

## 2. Two designs

Both satisfy F1–F8. They differ in six decisions that drive cost and robustness.

| Decision | Design A (plan as written) | Design B (alternative, recommended) |
|---|---|---|
| S1 Solver API | Free function `solve(K, f, T, opts)` + separate `setup()` | **Two-phase objects**: `setup(K)` returns a reusable operator; `solve(f, x, control)` per load case; uniform `footprint()` |
| S2 Parallel runtime | Reuse v1 `WorkerPool` (threads spawned per call) | **Persistent `KernelPool`**: threads parked on a barrier, **fixed-size chunks independent of thread count** → bit-identical reductions for any p |
| S3 Instrumentation | Error vs reference computed inside the loop | **Observer interface** with its time **measured and subtracted**; per-iteration timestamps; error tracking switchable |
| S4 Ordering input to the direct solver | Geometric ND from the mesher's grid | **Graph-based ND on the CSR graph** (level-structure bisection), with the grid planes as an optional hint; solvers never depend on `fem3d` |
| S5 Experiment harness | CLI command loops over runs in one process | **Manifest + one process per run + orchestrator**: resumable, OS-enforced memory cap, watchdog timeout, keep-awake |
| S6 Report numbers | Numbers typed into HTML | **Generated `numbers.json` + placeholders** in the HTML, filled by a script; build fails on unresolved placeholders or leftover [EST]/[VERIFY] |

---

## 3. High-level design (Design B)

### 3.1 Component diagram

```text
                       ┌──────────────────────── uniforge_cli ────────────────────────┐
                       │  study plan | study batch | study run | study collect |      │
                       │  study figures | (v1 commands unchanged)                    │
                       │  platform/: Process (spawn, wait w/ deadline, kill),        │
                       │             JobLimits (memory cap), Affinity (P-core pin),   │
                       │             KeepAwake                                        │
                       └───────────────┬─────────────────────────────────────────────┘
                                       │ uses
   ┌───────────────────────────────────▼──────────────────────────────────────────────┐
   │ uniforge_core                                                                     │
   │                                                                                   │
   │  study/   Case, RunSpec, Manifest, Runner (one run), Collect (CSV/JSON),          │
   │           Figures (SVG plots + PNG renders)                                       │
   │     │            │                 │                    │                         │
   │     ▼            ▼                 ▼                    ▼                         │
   │  fem3d/       solvers/           viz/              perf/ (v1)                     │
   │  Design       direct/  (ordering,  Raster3D          SystemInfo, Timer            │
   │  HexMesh       symbolic, numeric)   SurfaceField                                  │
   │  Assembly     krylov/ (PCG, J, IC0) Spy (density)                                 │
   │  BCs, Post    amg/    (SA setup,    Colormap                                      │
   │  Energy        V-cycle)              PngWriter (moved from app)                    │
   │     │            │                 │                                              │
   │     └─────┬──────┘                 │                                              │
   │           ▼                        │                                              │
   │        sparse/  CsrMatrix, Vector ops, KernelPool, SpGEMM, Transpose              │
   │           │                        │                                              │
   │           ▼                        ▼                                              │
   │        common/ (v1): Result, Error, CancelToken, ProgressFn, CsvWriter, MetaJson,  │
   │                      SvgPlot, AtomicFile, Rng                                     │
   └───────────────────────────────────────────────────────────────────────────────────┘

   tools/report_v2/ (Python stdlib + pypdf):  fill_numbers.py → report.html → Edge → PDF → check_pdf.py
```

### 3.2 Dependency rules (CMake-enforced, extend v1's lint)

| Module | May depend on | Must not depend on | Why |
|---|---|---|---|
| `sparse` | `common` | everything else | Shared, fair kernels |
| `fem3d` | `sparse`, `common` | `solvers`, `viz`, `study` | The physics knows nothing about how it is solved |
| `solvers` | `sparse`, `common` | **`fem3d`**, `viz`, `study` | Solvers see only matrices (+ optional coordinate hint as plain arrays). Keeps the comparison honest and the solvers reusable on any geometry |
| `viz` | `common` | `fem3d`, `solvers` | Takes plain arrays (positions, triangles, scalars) |
| `study` | all core modules | `app`, OS headers | Orchestration of one run, collection, figures |
| `cli/platform` | OS headers | – | Process, job limits, affinity, keep-awake live here, as v1's `Process_*` |

The CI lint gains one rule: no `#include "core/fem3d/..."` under `src/core/solvers/`.

### 3.3 Data flow of one run

```text
RunSpec (JSON) ─► Design ─► HexMesh ─► LinearSystem{K,f,free mask,energy terms}
                                              │
          reference T_ref (binary, cached) ──►│
                                              ▼
                 Solver.setup(K) ─► SetupReport ─► Solver.solve(f, x, control) ─► SolveReport
                                              │                                   │
                                              ▼                                   ▼
                              Post: T_max, R_th, fin heat, energy audit     RunResult.json (atomic)
                                              │
                                              └─► optional field dump (binary T) for figures
```

---

## 4. Data models

### 4.1 Geometry and mesh (`fem3d`)

| Type | Fields | Notes |
|---|---|---|
| `Material` | `id`, `name`, `k` (W/(m·K)), `q` (W/m³) | q nonzero only for the die |
| `FaceBc` | `kind ∈ {Insulated, Convection{h, Tinf}, Dirichlet{T}}` | One per box face; internal faces are ignored automatically |
| `Box` | `min[3]`, `max[3]` (m), `materialId`, `faceBc[6]` | Overlaps forbidden (validated) |
| `Design` | `name`, `vector<Box>`, `vector<Material>`, `ladderLevel` | Factories: `makeV1Bar()`, `makeCube()`, `makePlate()`, `makeHS7(params)`, `makePinFin(params)` |
| `MeshSpec` | `targetSpacing s`, `maxGradeRatio` (1.5), `timCells` (1 or 2) | Only mesh input (A1, A5) |
| `GridAxis` | `vector<double> lines` (sorted, mandatory lines included) | Graded, no slivers |
| `HexMesh` | `axis[3]`; `cellMaterial` (uint8 per grid cell, 0xFF = inactive); `nodeOfGridPoint` (int32 per grid point, −1 = unused); `nodeXyz` (double ×3); `boundaryFaces` (cell index, side, bc index); `stats` (cells, nodes, min/max/mean aspect ratio, max grade ratio) | Structured storage; compact node numbering in i-j-k order |

Memory at 3.2 × 10⁶ DOF [EST]: bounding grid of HS-7 at s = 0.2 mm ≈ 300 × 200 × 155 points ≈ 9 × 10⁶
→ `nodeOfGridPoint` 37 MB, `cellMaterial` 9 MB, `nodeXyz` 77 MB. Small next to K (~1 GB).

### 4.2 Sparse (`sparse`)

| Type | Fields | Notes |
|---|---|---|
| `CsrMatrix` | `n` (int32), `rowPtr` (int64, n+1), `col` (int32, sorted per row), `val` (double) | Full symmetric storage (SpMV speed); `lowerView()` gives the lower triangle for Cholesky/IC(0) without copying values where possible |
| `CsrPattern` | `rowPtr`, `col` only | Built once from grid connectivity; K allocated from it (A4) |
| `Vector` | `std::vector<double>` | Aligned allocator not needed for scalar code [EST] |
| `MemoryFootprint` | `bytes` by named component (`K`, `L`, `IC0`, `hierarchy[ℓ]`, `P[ℓ]`, `work vectors`) | Computed from `capacity() * sizeof` |

### 4.3 Linear system and BCs (`fem3d` → `solvers` boundary)

| Type | Fields | Notes |
|---|---|---|
| `LinearSystem` | `K`, `f`, `freeMask` (rows not eliminated), `dirichletRows` (saved unconstrained rows for reactions), `heatIn` (∫q dV, W), `convectionTerms` (for the audit) | What crosses into a solver is only `K`, `f` and the optional `CoordinateHint`; the rest stays in `fem3d` |
| `CoordinateHint` | `nodeXyz` pointer + `gridIndex` (i,j,k per node) or null | Plain arrays, so `solvers` does not include `fem3d` |
| `EnergyAudit` | `heatIn`, `heatConvected`, `reactions`, `imbalance` (W), `relImbalance` | Computed in `fem3d::post` from T; also `1ᵀr` over free rows from the solver for A3 |

### 4.4 Solver reports (`solvers`)

| Type | Fields |
|---|---|
| `SolverKind` | `DirectND`, `DirectRCM`, `PcgJacobi`, `PcgIc0`, `Amg`, `AmgPcg` (string labels as in plan §3.1) |
| `SolverOptions` | per-kind settings: ordering, IC(0) shift ladder, AMG strength θ, smoother, sweeps, coarse size, max levels |
| `SolveControl` | `relTol`, `maxIterations`, `wallTimeCap`, `memoryBudgetBytes`, `observer*`, `CancelToken*`, `ProgressFn` |
| `SetupReport` | `seconds`, `footprint`, kind-specific: `nnzL`, `predictedNnzL`, `flops` (direct); `ic0Shift`, `ic0Retries`; `levels[]{rows, nnz}`, `operatorComplexity`, `gridComplexity` (AMG) |
| `SolveReport` | `status ∈ {Converged, MaxIterations, Stagnated, Breakdown, OverBudget, TimeCap, Cancelled, Diverging}`, `failureDetail`, `iterations`, `workSpmvEq`, `seconds`, `observerSeconds`, `residualHistory`, `iterTimestamps`, `errorHistory` (optional), `kappaEstimate` (PCG), `finalTrueRelResidual`, `residualSum` (1ᵀr over free rows) |

A **failure is a status, not an `Error`**: `Result<SolveReport>` carries an `Error` only for misuse
(dimension mismatch, NaN input, I/O). `Breakdown`, `OverBudget` etc. are normal outcomes that the
harness records and plots as ✕ (A12). This split keeps "the solver failed on this case" (a result)
apart from "the program is wrong" (an error).

### 4.5 Run and campaign (`study`)

| Type | Fields | Notes |
|---|---|---|
| `RunSpec` | `runId` (stable hash of all inputs), `experiment` (E0–E8), `design` + params, `MeshSpec`, `solver` + options, `control`, `repetitions`, `referencePolicy`, `pinCore`, `dumpField` | One JSON object per run |
| `Manifest` | ordered `vector<RunSpec>`, budget per experiment, version of the build | Generated by `study plan`; ordering short → long (plan §5.2) |
| `RunResult` | `RunSpec` echo, mesh stats, assembly time/bytes, `SetupReport`, `SolveReport` per repetition (median/min/max), engineering outputs (T_max, R_th, fin heats), `EnergyAudit`, `peakWorkingSet`, `coreUsed`, `SystemInfo`, git commit, timestamps | Written atomically (`AtomicFile`), one file per run; its presence = run done |
| `ReferenceStore` | `refs/<meshHash>.bin` (raw doubles + header with n, solver, tolerance, residual) | Created by the reference runs, read by trace runs (A2) |

---

## 5. Interfaces (design notation)

### 5.1 Solvers

```text
interface LinearOperatorSolver
    setup(K: CsrMatrix, hint: CoordinateHint?, pool: KernelPool, opts: SolverOptions)
        -> Result<SetupReport>            // may return status OverBudget without allocating (direct)
    solve(f: Vector, x: Vector&, control: SolveControl) -> Result<SolveReport>
    footprint() -> MemoryFootprint
    // setup once, solve many times: load cases (E5) reuse the factor / IC(0) / hierarchy

factory makeSolver(kind: SolverKind) -> unique_ptr<LinearOperatorSolver>
```

Composition inside `solvers`:
- `direct/`: `Ordering` (RCM, graph-ND, grid-ND hint) → `Symbolic` (etree, column counts,
  predicted nnz(L) and bytes, budget check) → `NumericCholesky` (up-looking) → `TriangularSolve`.
- `krylov/`: `Pcg(precond: Preconditioner)`; `Preconditioner` = `Identity | Jacobi | Ic0 | AmgVCycle`.
- `amg/`: `Strength` → `Aggregate` → `TentativeP` → `SmoothP` (needs ρ(D⁻¹K) by power iteration)
  → `Galerkin` (SpGEMM KP, then Pᵀ(KP)) → `Hierarchy`; `VCycle` uses `Smoother`
  (`SymGaussSeidel` serial, `L1Jacobi` parallel) and a `direct/` factor on the coarsest level.
  `Amg` = `VCycle` as a stationary iteration; `AmgPcg` = `Pcg(AmgVCycle)`.

### 5.2 Observer (instrumentation, A2/A3)

```text
interface IterationObserver
    onIteration(k, residualNorm, x: const Vector&)   // called after each iteration
```

The solver wraps every call in two clock reads and accumulates `observerSeconds`; reported solve
time = wall − observer time. Built-in observers: `ResidualOnly` (no extra work),
`ErrorVsReference(T_ref)` (‖x − T_ref‖∞, one O(n) pass), `Snapshot(every m)` (for convergence
pictures). Check in V2-8: on small sizes, run with and without `ErrorVsReference` and confirm the
subtracted times agree within 2 % [EST] (N4).

### 5.3 Kernel pool (`sparse`)

```text
class KernelPool
    KernelPool(threads p, pinning plan)       // threads start once, park on a barrier
    parallelFor(rowRange, chunk = 4096 rows, body)   // static, deterministic chunk → thread map
    reduce(rowRange, chunk = 4096, partialBody) -> double
        // each chunk writes partial[c]; the caller sums partial[0..C) in index order
        // → result independent of p (bit-identical for p = 1 … 20)
```

Kernels on top: `spmv`, `dot`, `axpy`, `xpay`, `normInf`, `jacobiApply`, `l1JacobiSweep`.
Sequential kernels (`SymGaussSeidel`, IC(0) triangular solves, Cholesky) run on the calling thread.

### 5.4 Harness (`cli` + `study`)

```text
uniforge_cli study plan    --experiments E0..E8 --out results/v2/manifest.json
uniforge_cli study batch   --manifest ... [--dry-run] [--only E2] [--resume]
uniforge_cli study run     --manifest ... --run-id <id>        (one run, its own process)
uniforge_cli study collect --results results/v2/runs --out results/v2/csv
uniforge_cli study figures --csv results/v2/csv --out docs/report_v2/fig
```

`batch` (orchestrator, single-threaded, Windows Job Object + `SetThreadExecutionState`):
for each RunSpec without a `RunResult`: spawn `study run` in a Job with a hard memory limit
(budget + margin), pinned per policy; wait with a deadline (wall cap + margin); on timeout kill
and write a `RunResult` with status `TimeCap`; on crash write `Crashed` with exit code; append to
`batch.log`; update `progress.json` (done / total / ETA). A reboot or Ctrl+C loses at most the run
in progress.

---

## 6. Deep dive: cost hot spots and how the design handles them

| Hot spot | Cost (rough) [EST] | Design response |
|---|---|---|
| SpMV (all iterative solvers) | ~12 B × nnz moved per call: ~1 GB at 3.2 × 10⁶ DOF → 50–100 ms single core | One shared CSR kernel for all solvers (N1); 32-bit columns; rows in i-j-k order (locality from the structured numbering) |
| **Thread start-up per kernel call** (v1 `WorkerPool`) | ~20 threads × tens of µs per call [EST] × 4–6 calls per PCG iteration × thousands of iterations → seconds, and it dominates small meshes | **Persistent `KernelPool`** (S2) |
| Parallel reductions | Non-deterministic if chunking depends on p | Fixed 4096-row chunks, ordered final sum (N2) |
| Error tracking (A2) | One O(n) pass, ~5 % of an SpMV's bytes [EST] | Observer time subtracted, verified (S3) |
| Direct numeric factorisation | O(n²) flops (ND, 3D) [VERIFY]; simplicial ~0.5–1 GFLOP/s [EST] | Symbolic phase first: predicted nnz(L) and bytes vs budget → `OverBudget` before any big allocation |
| AMG setup (SpGEMM) | Several solves' worth [EST]; temporary memory for KP | Two-step product with exact-size allocation via a symbolic pass; setup timed separately; reused across load cases |
| Assembly | 64 entries per hex scattered | Pattern from grid connectivity, local→CSR index map per element type position, no COO (A4) |
| Run-to-run variance | P/E cores, turbo | Pinning to one P-core for serial runs; repetitions by run time (A6, A8) |

### 6.1 Memory model at the largest size (3.2 × 10⁶ DOF, [EST] throughout)

| Component | Bytes | Share of 32 GB |
|---|---:|---:|
| Mesh (grid maps + coordinates) | ~0.12 GB | <1 % |
| K (CSR, 8.6 × 10⁷ nnz) | ~1.05 GB | 3 % |
| PCG-J work vectors (5 × n doubles) | ~0.13 GB | <1 % |
| IC(0) factor (lower triangle) | ~0.55 GB | 2 % |
| AMG hierarchy (op. complexity 1.3–1.8) + P matrices | ~0.6–1.1 GB | 2–3 % |
| Reference + error observer | ~0.05 GB | <1 % |
| Direct (would need nnz(L) far beyond budget at this size) | refused by symbolic check | – |

Everything except the direct solver fits comfortably, so the 8 GB solver budget (D10) only
binds the direct solver, as intended.

---

## 7. Robustness

### 7.1 Failure taxonomy (A12) and where it is detected

| Failure | Detected in | Status / Error | Shown in report |
|---|---|---|---|
| Predicted fill over budget | `direct::Symbolic` | `OverBudget` (no allocation) | ✕ on memory/time plots |
| Non-positive pivot in Cholesky | `direct::NumericCholesky` | `Breakdown` + pivot index | E8 singular case |
| IC(0) pivot ≤ 0 | `krylov::Ic0` | retry with shift ladder; `Breakdown` after the last shift | E3/E8, H8 |
| pᵀKp ≤ 0 | `krylov::Pcg` | `Breakdown` (matrix not SPD) | E8 |
| No 10× residual drop in N iterations | `Pcg`, `Amg` | `Stagnated` | E3 extremes |
| Residual growth per cycle > 1 | `Amg` | `Diverging` | E3/E8 |
| Iteration / wall-time cap | all iterative | `MaxIterations` / `TimeCap` | ✕ |
| OS memory limit hit / crash | orchestrator | `Crashed` with exit code | ✕ + log line |
| NaN/Inf in input or result | every solver entry/exit | `Error{NonFinite}` | never plotted; fails the run |
| Converged but wrong mean (near-singular) | `ErrorVsReference`, `residualSum`, `EnergyAudit` | flagged in `RunResult` | discussed in E3 (A3) |

### 7.2 Correctness guards (tests per module)

| Module | Guards |
|---|---|
| `sparse` | CSR invariants (sorted, no duplicates, symmetric where claimed); reductions bit-identical for p = 1, 2, 7, 20; SpMV vs dense |
| `fem3d` | volume/area exact; watertight surface; grade ratio ≤ 1.5; patch test; row sums 0 before BCs; energy audit; v1 bar = 188.2 °C; MMS slope 2; fin theory |
| `solvers/direct` | vs dense oracle; all orderings same solution; predicted nnz(L) = actual nnz(L) exactly |
| `solvers/krylov` | model-problem iteration counts; κ_est vs analytic κ (1D Laplacian) within a factor; shift ladder exercised by a constructed non-M-matrix |
| `solvers/amg` | P preserves constants; Kc symmetric and positive definite; V-cycle preconditioner symmetry test (A15); two-grid factor bound |
| `study` | RunSpec hash stable; a killed run resumes; collected CSV = golden on tiny manifest (CI) |
| report tools | unresolved placeholder, [EST]/[VERIFY] residue or code-like text → build fails |

---

## 8. Report pipeline (Design B, S6)

```text
results/v2/runs/*.json ─► study collect ─► results/v2/csv/*.csv + numbers.json
                                              │                      │
                       study figures ◄────────┘                      │
                        │  SVG plots (SvgPlot + tokens A13)          │
                        │  PNG renders (viz::Raster3D) ─┐            │
                        ▼                               ▼            ▼
              docs/report_v2/fig/*.svg|png   annotations as SVG    report.src.html with {{e2.crossover_dof}}
                                             overlays (no font      │
                                             rendering in C++)      ▼
                                                      tools/report_v2/fill_numbers.py → report.html
                                                                    │
                                                         Edge headless print → UniForge_V2_Report.pdf
                                                                    │
                                                         check_pdf.py (pypdf): pages, size, text scan
```

- **Renders**: `viz::Raster3D` is a CPU z-buffer rasteriser (orthographic/perspective camera,
  per-vertex scalar → colormap, Lambert shading, optional edges). Labels, legends and arrows are
  SVG overlays in the HTML, so the C++ side never renders text. Cut planes are cheap on a tensor
  grid (a grid plane is a set of quads with nodal T).
- **PNG size**: v1's `PngWriter` writes stored (uncompressed) deflate; a 1600 × 1000 RGBA render is
  ~6.4 MB. Whether Edge recompresses images in the PDF is [VERIFY]; `check_pdf.py` reports the PDF
  size and flags > 15 MB, and the fix if needed is a smaller render size or a real deflate encoder.
- **Numbers**: `numbers.json` is the single source for every number in the prose (crossover DOF,
  slopes, speedups, T_max). `fill_numbers.py` replaces `{{key}}` and writes a provenance comment
  per number (run IDs). No number is typed by hand (N8).
- **"No code" check**: `check_pdf.py` scans the extracted text for code markers (`#include`,
  `std::`, `;` followed by `}` patterns, `for (`) and fails on a hit.

---

## 9. Scale and reliability of the campaign

| Concern | Mechanism |
|---|---|
| Overnight crash / reboot | One result file per run; `batch --resume` skips finished runs |
| Runaway memory | Windows Job Object memory limit per child (budget + margin), plus the direct solver's symbolic refusal |
| Hang / slow run | Orchestrator deadline → kill → `TimeCap` result |
| PC sleeps | `SetThreadExecutionState(ES_SYSTEM_REQUIRED)` during `batch` (D12 still asks you to keep it idle) |
| Monitoring | `progress.json` (done, total, ETA, current run) and `batch.log`; a one-line status per run on the console |
| Noise | Idle check before each run (CPU load from `SystemInfo`), pinning, repetitions by run time; result records load before/after |
| Reproducibility | RunSpec hash + git commit + build flags in every `RunResult`; `study batch` refuses a dirty tree for official runs |

---

## 10. Evaluation: Design A vs Design B on cost and robustness

Scores 1–5 (5 best), my judgement. "Cost" = machine time and memory of the campaign plus
development effort.

| Criterion | A | B | Reason |
|---|:-:|:-:|---|
| **Time: measurement fidelity** | 2 | 5 | A spawns threads per kernel call (v1 pool) and mixes error tracking into solve time; B parks threads and subtracts observer time |
| **Time: campaign machine time** | 3 | 4 | B's observer avoids a second (trace) pass and its resume avoids reruns; both have caps |
| **Memory: predictability** | 3 | 5 | B: footprints by component, symbolic-first direct, OS memory limit per run; A: process-level only |
| **Memory: assembly peak** | 4 | 5 | Both assemble into CSR (A4); B builds the pattern once and shares it with the solvers' setup |
| **Iterations: comparability** | 3 | 5 | B: work units, timestamps per iteration, deterministic for any p, so iteration counts are exact facts |
| **Mesh size scaling evidence** | 4 | 4 | Same ladder (A1) in both |
| **Conditioning robustness** | 3 | 5 | B: failure statuses distinct from errors, κ_est, residualSum and energy audit in every result, IC(0) shift ladder |
| **Generality (complex geometry claim)** | 2 | 4 | A's grid-plane ND only works on the tensor grid; B's graph ND works on any mesh, and solvers never see `fem3d` |
| **Campaign resilience** | 2 | 5 | A: one process, a crash loses the night; B: per-run processes, resume, watchdog |
| **Report traceability** | 2 | 5 | A: hand-copied numbers; B: generated numbers with provenance and build-time checks |
| **Development effort** | 5 | 3 | B adds KernelPool (~1–2 h), observer (~1 h), graph ND (~2–3 h), orchestrator + job limits (~2–3 h), report scripts (~2 h): **~8–11 h more** [EST] |
| **Risk of bugs in infrastructure** | 4 | 3 | More moving parts in B, mitigated by its tests (§7.2) |
| **Total (of 60)** | **37** | **53** | |

**Recommendation: Design B**, with one cost-saving option: if the deadline (D1) is short, keep
**grid-plane ND** (A's S4) instead of graph ND. That saves ~2–3 h and loses only the generality
argument, which the report can state as a limitation. The persistent KernelPool, observer
subtraction, per-run processes and generated numbers are not optional: without them the timing
and memory numbers, the core of the notice's question, are not trustworthy.

Effort impact on the plan rev 2 (reviewed column): +8–11 h → **~43–60 h + one overnight run**
[EST]. In exchange the campaign can be rerun or resumed without risk, which should save at least
one rerun of the overnight batch.

---

## 11. Mapping to plan phases

| Plan phase (rev 2) | Design B work added or moved |
|---|---|
| V2-1 sparse | `CsrMatrix`, `CsrPattern`, **`KernelPool`** with deterministic reductions, SpGEMM + transpose (moved here from AMG so it is tested early) |
| V2-2 mesher | `Design`, `HexMesh`, factories, `CoordinateHint` export, surface triangles for `viz` |
| V2-3 PCG-J | `LinearOperatorSolver` interface, `SolveControl`, `SolveReport`, observers, κ_est |
| V2-4 FEA | `LinearSystem`, `EnergyAudit`, `fem3d::post` |
| V2-5 direct | `Ordering` (RCM, **graph ND**, grid-ND hint), symbolic budget check |
| V2-6 IC(0) | shift ladder |
| V2-7 AMG | as §5.1 |
| V2-8 harness | `RunSpec`, `Manifest`, `study run/batch/collect`, **Job Object + affinity + keep-awake** in `cli/platform`, `ReferenceStore` |
| V2-9 figures | `viz::Raster3D`, `Spy`, `Colormap`; move `PngWriter` from `app/render` to `core/viz` (the app includes it from there; v1 behaviour unchanged) |
| V2-10 report | `fill_numbers.py`, `check_pdf.py`, `report.src.html` |

---

## 12. What to revisit as the system grows

- **Direct solver**: supernodal factorisation (dense blocks, BLAS-3 style kernels) would move the
  memory/time wall; out of scope, named in the report's "next steps".
- **AMG setup** is serial; parallel aggregation and SpGEMM would matter beyond ~10⁷ DOF.
- **Matrix-free SpMV** on the tensor grid would cut PCG memory by ~10× [EST] but would make the
  comparison unfair (different operator storage per solver), so it stays out of V2.
- **Unstructured tetrahedral meshes** (curved geometry): `solvers` already allow it (graph ND,
  CSR only); `fem3d` would need a tet element and an external mesher.
- **Mixed precision / GPU**: not needed for the question asked.
- **Transient and nonlinear k(T)** (plan optional items): `setup` once / `solve` many maps directly
  onto implicit time stepping and Picard iterations.

---

## 13. Decisions this design adds

| ID | Decision | Recommendation |
|---|---|---|
| S-D1 | Adopt Design B (vs A) | **B** |
| S-D2 | Graph ND (general) vs grid-plane ND (cheaper) | Graph ND unless the deadline is short |
| S-D3 | Move `PngWriter` from `app/render` to `core/viz` (touches v1 code, behaviour unchanged) | Yes |
| S-D4 | Report tooling in Python stdlib + pypdf (installed) vs PowerShell as in v1 | Python for fill/check; keep v1's Edge print step |
| S-D5 | Official batch refuses to run on a dirty git tree | Yes |
