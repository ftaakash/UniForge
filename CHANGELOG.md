# Changelog

All notable changes are recorded here, one section per development phase.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased] - V2 (branch `v2`)

V2 answers the Assignment Extension Notice: three solver types on a 3D thermal analysis of a
more complex object, reported in one PDF. Specification:
`docs/design/v2/UniForge_V2_Build_Spec.md`. `main` and `v1.0.0` are unchanged.

### Phase V2-8: Experiment harness and official batch (`v2-phase-8`)
- Official batch on commit `5e9bea7` (2 h 46 min): 241/241 runs left a result (205 converged,
  23 stand-alone AMG stagnations, 7 direct runs over the memory budget, 6 singular runs
  detected). Results in `results/v2/` (references not committed); first reading of the
  hypotheses in `docs/v2_experiments.md`, including a suspected AMG strength-threshold
  weakness on trilinear hexahedra.
- Fixed after the batch: `batch.log` kept only its first line on Windows (old log still open
  during the atomic rename) and the final `progress.json` update was missing; the smoke test
  now checks both.
- `src/core/study/`: code-defined plan of E0–E8 (241 runs; dry run 85) with ids a child process
  resolves on its own; one run (mesh, assembly, solver setup and timed solves with 5/3/1
  repetitions by run time, error observer on the first, iteration history, energy audit) and
  references (direct ND within 8 GB, else AMG-PCG to 1e-12); collect into experiment CSVs, E5
  load cases (setup + k·solve) and E2 time-to-accuracy.
- `uniforge_cli study plan | run | batch | collect`: one process per run, Windows Job Object
  memory cap and timeout kill, single-thread runs pinned to a performance core, keep-awake, CPU
  load before each run, resume after a crash, refuses a dirty tree for official runs.
- Dry run (85 runs, all left a result) found that the direct solvers returned a 2e13 °C
  "solution" on the singular all-insulated case; `DirectCholesky` now reports Breakdown for a
  pivot below 1e-13 of its diagonal and for a solve whose relative residual exceeds 1e-6.
- `docs/v2_experiments.md` (harness and runbook); 8 study tests, a batch smoke test with
  resume, 1 direct-solver test.

### Phase V2-7: Smoothed-aggregation AMG (`v2-phase-7`)
- `src/core/solvers/amg/Amg`: strength of connection with a threshold halving per level,
  three-phase greedy aggregation (isolated nodes left out), piecewise-constant tentative
  prolongator, prolongator smoothing with the filtered matrix and a Gershgorin ω, Galerkin
  coarse operators (symmetrised exactly), direct solve on the coarsest level, symmetric V(ν,ν)
  cycle with forward/backward Gauss–Seidel. `AmgSolver` (stand-alone, AMG) and
  `AmgPreconditioner` (AMG-PCG); `makeSolver` now builds all six variants.
- Setup report gains rows and nnz per level and operator/grid complexity; the PCG setup report
  carries the full preconditioner report.
- Measured: h-independent stand-alone convergence on the 3D Laplacian (15–19 cycles from 10³ to
  32³); on HS-7 at 2 mm AMG-PCG needs 84 iterations (PCG-IC0 117, PCG-J 800), while stand-alone
  AMG needs 673 cycles because of the thin TIM.
- 7 AMG tests (aggregation, constants preserved, Galerkin symmetry and definiteness, V-cycle
  symmetry, size independence, AMG-PCG vs IC(0) and Jacobi, statuses) and 1 verification test.

### Phase V2-6: IC(0) preconditioner (`v2-phase-6`)
- `src/core/solvers/krylov/Ic0`: incomplete Cholesky with zero fill on the lower-triangle
  pattern; shift ladder K + α·diag(K), α = 0, 1e-3, 1e-2, 1e-1, 1, with the α used and the
  failed attempts reported; Breakdown when the ladder is exhausted. `makeSolver` builds PCG-IC0.
- The PCG setup report now passes on the preconditioner's timing, detail, shift and retries.
- Measured: on HS-7 at 2 mm PCG-IC0 needs 117 iterations against 800 for PCG-J (κ_est 2.0e3 vs
  1.37e5) and no shift, although K has 68 872 positive off-diagonal entries; Kershaw's 4 × 4
  matrix breaks unshifted IC(0) and is recovered with α = 1.
- Spec correction: positive off-diagonals of trilinear boxes appear as soon as an edge is longer
  than the other two, not above an aspect ratio of √2 (a 2D result).
- Fix: GCC 13 ASan/UBSan false positive (-Warray-bounds) that failed the Linux build of
  `v2-phase-5`.
- 5 IC(0) tests, 1 element test, 1 verification test.

### Phase V2-5: Sparse direct Cholesky (`v2-phase-5`)
- `src/core/solvers/direct/Ordering`: natural, reverse Cuthill–McKee (pseudo-peripheral start,
  per component) and graph nested dissection (middle-level separators of breadth-first level
  structures, RCM on leaves); disconnected graphs handled.
- `src/core/solvers/direct/Cholesky`: `DirectCholesky` with timed ordering, symbolic (elimination
  tree, exact column counts, memory-budget check before allocating L) and numeric (up-looking)
  phases; triangular solves; Breakdown on a non-positive pivot; `makeSolver` builds DIRECT-ND
  and DIRECT-RCM.
- Preliminary HS-7 measurements (spec §5.2): nnz(L) predicted exactly; ND gives the smaller
  factor at every size and is faster from 285 k DOF; RCM exceeds 8 GB at 568 k DOF, ND factors
  568 k DOF in 327 s.
- Verification: both orderings reproduce PCG-J on the v1 bar and HS-7 to 3.8e-10 K.
- 10 unit tests and 1 verification test.

### Phase V2-4: 3D finite elements and verification (`v2-phase-4`)
- `src/core/fem3d/Element`: trilinear hexahedron stiffness and load as tensor products of 1D
  matrices (exact on boxes), bilinear face convection matrix and loads, Gauss quadrature for
  non-uniform sources and ambients.
- `src/core/fem3d/Assembly`: pattern from grid connectivity, assembly straight into CSR,
  convection, symmetric Dirichlet elimination with saved rows for reactions, assembly time and
  matrix size. Measured on HS-7: 4.7 s and 1.16 GB for 3 759 720 DOF.
- `src/core/fem3d/Post`: energy audit, reactions, peak temperature, trilinear sampling, L2 and
  nodal error norms.
- Verification (`tests/numerical/fem3d`, solved with PCG-J): harmonic trilinear patch test exact
  with Dirichlet and with convection faces; the v1 bar in 3D reproduces 188.2 °C at 0.42 m and
  0.42 / 0.58 W through the ends; a manufactured solution converges at order 2.00; a single fin
  matches 1D fin theory (heat −0.005 %, tip within 0.03 K); HS-7 energy balance to 2e-12.
- A deliberately broken Dirichlet elimination fails the patch, bar and fin tests.
- 12 unit tests and 6 verification tests.

### Phase V2-3: Solver interface and Jacobi-PCG (`v2-phase-3`)
- `src/core/solvers/`: two-phase solver interface (setup once, solve per load case); a method
  failure (breakdown, iteration or time cap, stagnation, over budget, cancelled) is a status,
  misuse is an error. `makeSolver` builds PCG-J; the other kinds name the phase that adds them.
- Solve report: solver time with observer time excluded, per-iteration timestamps, residual
  history, SpMV count, bytes moved and SpMV-equivalents, κ estimate from the CG–Lanczos
  tridiagonal, true residual and the masked residual sum (heat imbalance).
- Observers `ErrorVsReference` (time-to-accuracy) and `Snapshot`.
- PCG with identity (tests only) and Jacobi preconditioners; bit-identical for any thread count.
- `tests/lint/module_rules.cmake`: CTest check that solvers, sparse and viz do not include fem3d.
- 15 unit tests plus the lint check.

### Phase V2-2: Geometry and mesher (`v2-phase-2`)
- `src/core/fem3d/Design`: boxes with materials and per-face boundary conditions, validation,
  and the ladder objects v1 bar (L0a), cube (L0b), plate (L1), finned plate (L2), HS-7 (L3) and
  pin fin (L4).
- `src/core/fem3d/HexMesh`: graded tensor-grid mesher (every box face is a grid line, neighbour
  size ratio ≤ 1.5, fixed TIM layers), compact node numbering, boundary faces tagged with box
  and side, outward surface triangles.
- Measured HS-7 mesh sizes along the spacing ladder (56 015 … 3 759 720 nodes) recorded in the
  build specification §3.4.
- 19 unit tests, including exact volume and convective area, a watertight outward surface and
  an STL round trip through the v1 importer.

### Phase V2-1: Sparse kernels (`v2-phase-1`)
- `src/core/sparse/`: CSR matrix and pattern (32-bit columns, 64-bit offsets) with validation;
  persistent `KernelPool` whose fixed 4096-row chunks make reductions bit-identical for any
  thread count; dot, norms, axpy, xpay, SpMV, residual; transpose and SpGEMM with exact allocation.
- 29 unit tests.

### Phase V2-0: Design (`v2-phase-0`)
- `docs/design/v2/`: analysis and plan, design review (amendments A1–A15), system design
  (Design A vs B) and the merged V2 build specification; `docs/report_v2/figure_tokens.md`.

## [1.0.0] - 2026-11-02

### Phase 9: Delivery
- Report `docs/report/report.pdf` (5 pages, A4), built from `docs/report/report.html` with
  headless Edge by `tools/build_report.ps1`: design choices, import limits, FEA verification,
  benchmark and its analysis, embedded design, backprop check, tests, limitations, AI use.
- `docs/ownership.md` (original vs reused work, AI assistance), `docs/walkthrough.md` (where is X,
  how to change Y, how to rerun Z, numbers to know), `docs/demo_script.md` (4-minute demo).
- Screenshots of every view from the packaged app in `docs/demo/screenshots/`.
- README rewritten for the release: status, evidence per task, package contents, limitations,
  documentation index.
- `package.ps1` adds `git/UniForge.bundle` (verified with `git bundle verify`) and
  `git/git_log.txt` to the package.

### Phase 8: Hardening (trimmed)
- Crash handler (`platform/CrashHandler`): an unhandled SEH exception or `std::terminate`
  writes `logs/crash_<pid>.txt` (version, commit, UTC time, exception code, address) and shows
  a message box naming the file. No minidump, so no `dbghelp.dll` import. `--simulate-crash`
  exercises it.
- `tools/edge_case_runs.ps1` → `results/edge_cases.md`, all pass: Launch.exe from a path with
  spaces and non-ASCII characters (ready in 0.122 s), `--force-warp`, a read-only package folder
  (logs fall back to `%LOCALAPPDATA%\UniForge`), the simulated crash, and `uniforge_cli all`
  into a non-ASCII output folder.
- Overview: rows D–F now show live status (D: last benchmark pass/fail and speedup; E: the
  simulated-device notice; F: the brief's gradient check, computed on the spot) instead of
  "Pending · Phase 5/6/7". The unused `PlaceholderView` is removed.
- Not done (cut for the deadline): 200 % DPI and full keyboard pass, high-contrast check.

### Integration (Phases 4–7)
- All four phase branches merged. `AppShell` registers every view; the CLI `all` runs fea,
  backprop, gen-samples and a quick bench and returns the worst exit code.
- Fixed: Hardware telemetry sparklines shared one ImGui ID, which triggered Dear ImGui's
  "conflicting ID" error when a device was connected.
- Fixed: the run bar's progress bar was wider than narrow panels and lost its label (a blank
  blue bar after a Thermal study). It now shrinks to fit and wraps when there is no room.
- Fixed: the device thread marked itself busy during every 10 Hz telemetry tick, so the
  Hardware action buttons flickered and a Disconnect click could be ignored. Only user
  commands set busy now.
- Fixed: `tests/CMakeLists.txt` lost a closing line in the merge.
- Package renamed to `RA2311042010060_Workbench.zip`; licence holder set to Aakash G S.

### Phase 7: Backpropagation (Task F)
- `core/ml`: `Params`, `Grads`, `Forward`, `forward` (z = wx + b, h = tanh z, ŷ = vh + c,
  J = ½e²), hand-derived `backward` (dc = e, dv = e·h, db = e·v·(1 − h²), dw = db·x; no
  autodiff), `sgdStep` (simultaneous update), `centralDiff` (ε = 1e-6, per-parameter pass at
  |Δ| ≤ 1e-5, NaN fails), `train` (steps + 1 losses), `BackpropProblem::validate()`.
- Measured: h = ŷ = 0.462117, e = −1.537883, J = 1.182542; dw = db = −1.209464,
  dv = −0.710682, dc = −1.537883; max |analytic − numeric| = 1.13e-10; after one update
  w = 0.620946, b = 0.120946, v = 1.071068, c = 0.153788. Loss decreases at every one of the
  100 steps, 1.182542 → 1.70e-20, with the per-step ratio settling at 0.634606.
- `uniforge_cli backprop`: `results/csv/backprop_gradients.csv`, `backprop_update.csv`,
  `loss_curve.csv` (+ `.meta.json`), `results/plots/loss_curve.svg`, `loss_curve_logy.svg`;
  prints the tables, a loss summary and PASS/FAIL checks. `all` now runs `fea` + `backprop` +
  `gen-samples`.
- Backprop view: inputs with validation and reset to the brief, forward / gradients / update
  cards, ImPlot loss curve with log-y toggle, explanation computed from the run, badges
  "4/4 FD checks", "Update matches brief", "Loss non-increasing".
- `SvgPlot`: log axes spanning more than 4 decades keep powers of ten only (thinned to ≤ ~10
  labels); plots up to 4 decades are byte-identical to before.
- Docs: `docs/backprop.md` (derivation, FD error budget, measured loss curve: rate, tanh
  saturation), `docs/appendix_handwritten/CHECKLIST.md` and a placeholder README (scans to
  come from me).
- Tests: 17 new (193 in total): `numerical_ml` (11), `negative_ml` (4), SvgPlot wide log axis,
  `integration.cli_backprop_matches_golden` (CSVs = `tests/golden/backprop/`, `backprop` and
  `all` byte-identical).

### Phase 6: Embedded integration (Task E)
- `docs/embedded_design.md` + `docs/embedded_block_diagram.svg` (light/dark): interface and
  pin-voltage table for NUCLEO-F446RE, Basys 3 and J-Link EDU Mini, every value cited to the
  vendor document (ST UM1724 Rev 17, ST DS10693 Rev 11, Digilent Basys 3 reference manual,
  SEGGER EDU Mini specification and knowledge base) with unverified assumptions marked; why a
  JTAG probe is not a compute board; the ST-LINK vs external-probe ownership conflict (CN2
  jumpers); safe integration plan; limitations. All three boards kept. The EDU Mini's 9-pin
  0.05" connector, 1.8/3.3 V interface and Cortex-M scope make the Nucleo's STM32F446 its target.
- `core/hw`: `IDeviceAdapter`, `MockTransport` (seed, latency, jitter, `FailMode` None /
  TimeoutOnce / TimeoutAlways / Disconnect), `DeviceStateMachine`, `RetryPolicy` (250 ms, 3
  retries at 100/200/400 ms, "recovered after n retries" or Faulted with a hint), injectable
  `SystemClock` / `ManualClock`, `McuAdapter` (vdd_V, die_temp_C), `FpgaAdapter` (xadc_temp_C,
  done), `JtagAdapter` (vtref_V, idcode), `discoverDevices()`. Every adapter is `simulated`.
- `DeviceManager` service (own thread, 10 Hz tick, command queue, 12 s telemetry ring buffers)
  and the Hardware view (simulated banner, Discover, three DeviceCards with SIMULATED chip, state,
  capabilities, telemetry + sparkline, Connect/Disconnect, Inject timeout, Retry/Reset fault,
  retry trail, "Design notes →").
- Tests: 23 new (199 in total): `unit_hw`, `negative_hw` (ManualClock, instant) and
  `DeviceManager.*`.

### Phase 5: Multicore (Task D)
- `core/common/Rng`: `std::mt19937_64` with UniForge's own [a, b) mapping (top 53 bits × 2⁻⁵³),
  so batches are identical on MSVC and GCC; pinned test vectors.
- `core/perf`: `generateBatch` (k ~ U[5, 50) W/(m·K), q ~ U[1e3, 5e4) W/m³, brief values for
  L, A, T0, TL), bounded `WorkerPool` (p `std::thread`s, chunk = max(1, cases/(8p)) from an
  atomic index, per-index writes, exceptions → `Error{Internal}` after join, cancel per case),
  `runSerial` / `runParallel` sharing `solveCase` (bitwise-equal results, index order kept,
  no result allocation in the timed region), `benchmark` (p ∈ {1, 2, 4} capped at the logical
  CPUs, 1 warm-up + 5 timed reps, median/min/max, speedup, efficiency, max |ΔT| over every node
  of every rep, order check, single-CPU disclosure), `SystemInfo` (`_win.cpp`:
  CPUID, `GetLogicalProcessorInformationEx` incl. P/E split, RAM, `RtlGetVersion`;
  `_linux.cpp`: `/proc`, `/etc/os-release`; compiler + flags from a build define) and
  `BenchmarkReportIO` (JSON, CSV, Markdown, three SVG plots, `.meta.json`).
- Report preset is 10 000 cases × 8 000 elements: the planned 4 000 × 4 000 measured
  T1 = 0.21 s, below the 1 s target.
- `uniforge_cli bench` (`--quick`, `--report`, `--seed`, `--cases`, `--elements`, `--workers`,
  `--status`) → `results/benchmarks/bench_<UTC date>.*`; `all` adds a quick bench in
  `results/benchmarks/quick/` (git-ignored).
- Performance view: parameters with worker cap note, Quick/Report presets, run bar
  ("Rep 3/5 · p = 2", Esc cancels, previous result kept), badges (agreement, order, 5 reps +
  warm-up), single-CPU warning, ImPlot tabs Timing / Speedup & efficiency / System, per-p
  table, Export report. New `ResultStore::benchmark` slot.
- Provisional report (`results/benchmarks/bench_2026-11-01.*`): T1 = 1.067 s, speedup 2.01 at
  p = 2 and 4.03 at p = 4, max |ΔT| = 0 °C. Labelled provisional; `tools/bench_report.ps1`
  regenerates the files and the results section of `docs/performance.md` in one command.
- Official report (regenerated on an idle machine during integration): T1 = 1.073 s, speedup
  2.00 at p = 2 and 4.00 at p = 4 (efficiency 100 %), max |ΔT| = 0 °C.
- `docs/performance.md`: method, results, overhead, memory, Intel hybrid/turbo, Amdahl,
  limitations.
- CI: `linux-asan` (GCC 13, ASan + UBSan, full suite) and `linux-tsan` (perf + services tests)
  on ubuntu-24.04.
- Tests: 39 new (215 in total): `unit_perf`, `numerical_perf` and Rng tests in `unit_common`.

### Phase 4: Visual views (Tasks B and C UI)
- Geometry view (Task B): STL and nodal-CSV file fields (native Open dialog, Explorer
  drag-drop), "Load samples" for both files in `data/`, imports on the job thread with
  progress and cancel; a failed or cancelled import keeps the previous model and shows the
  parser's message + hint (with line number) in an alert. Show mesh / points, point size [px],
  `◇ SYNTHETIC DATA` chip, NodeInspector (id, x/y/z [m], T [°C]), import summary, badges
  STL valid / CSV valid / non-finite rejected.
- 3D viewport (`render/`): offscreen D3D11 target shown with `ImGui::Image`, MSAA 4× when the
  device supports it (also on WARP); flat-shaded mesh with optional wireframe; instanced
  camera-facing point quads coloured with an inferno 1D texture; z-up orbit camera (LMB orbit,
  MMB / Shift+LMB pan, wheel zoom to the cursor, `F` fit = bbox sphere in the narrower FOV,
  `R` reset, `W` wireframe); axes triad, legend `T [°C]` (min/max + 5 ticks), bbox dimensions;
  CPU picking of the nearest node within 6 px with a highlight ring. Mesh and points are
  recentred on their bbox in double before the float conversion; GPU resources are rebuilt
  after a device loss. Shaders are fxc-compiled at build time.
- Thermal Analysis view (Task C): parameters with ranges, brief defaults and ↺ reset; mesh
  checkboxes 10/20/40/80; RunBar (`Ctrl+Enter` / `Esc`); ImPlot tabs T vs x (Okabe–Ito colour
  and marker fixed per N, exact solution dashed on top), max midpoint error vs h (log–log with a
  dashed slope-2 guide) and midpoint errors along x; convergence table (mono, right-aligned,
  sortable, `Ctrl+C` TSV, pass column); Export ▾ convergence / nodal / midpoint CSV (atomic,
  `%.17g`) and the current plot as PNG; badges midpoint error = qh²/8k (≤ 1e-9 °C), order ≈ 2
  (± 0.01), energy balance (≤ 1e-9 W), T_max @ x* (within h/2, nodal error ≤ 1e-9 °C), each
  with measured value and tolerance in the tooltip.
- Overview: rows A–F; A, B and C show live status, the rest "Pending".
- Widgets (`ui/widgets/`): VerificationBadge, InlineAlert, ParamField, FileField, RunBar,
  DataTable, ExportMenu, ColorLegend, NodeInspector, Viewport3D; `ui/Format` (spec §8.3) and
  `ui/ThemeViz` (data-viz colour tokens).
- Tests: 34 new (210 in total): number formatting, camera fit/orbit/pan/zoom maths, picking
  (radius, nearest, depth tie-break, behind camera), PNG encoder, and the UI-less import →
  render-model pipeline and study job/exports (`integration_views`).
- Not done: Overview "Run all self-checks" (cut), screenshots (see `docs/demo/phase4/README.md`).

### Phase 3: Importers (Task B data side)
- `core/io`: `LineReader` (fixed 64 KB buffer, LF/CRLF, UTF-8 BOM, progress = bytes read / size,
  cancellation and byte limit checked per buffer refill, line-length limit), locale-independent
  number parsing with `std::from_chars` (rejects NaN, Inf, overflow, trailing text),
  `ImportLimits`, `ImportStats`, `Vec3d`, an ASCII STL writer and shared error builders.
- `parseAsciiStl` / `parseAsciiStlFile`: keyword state machine; separate messages for a 0-byte
  file and a solid without facets; binary STL sniffed (NUL bytes, or size = 84 + 50·n) and
  rejected with "Export as ASCII STL"; wrong vertex or coordinate count, unknown or misplaced
  keyword, EOF inside a facet; zero normals recomputed, other normals normalised; degenerate
  triangles counted as a warning; bbox; several solids merged; missing `endsolid` is a warning.
- `parseThermalCsv` / `parseThermalCsvFile`: exact header `node_id,x_m,y_m,z_m,temperature_C`
  (trimmed); per-row errors with line numbers for missing/extra columns, quoted fields, empty or
  non-numeric values, non-integer ids, NaN/Inf, duplicate ids (also in unsorted files),
  T < −273.15 °C; header-only file → "No data rows"; Tmin/Tmax, bbox; `syntheticFlag` from a
  `SYNTHETIC` file name.
- Error copy follows spec §8.5: "Line N: <what>" plus a hint on how to fix it.
- `uniforge_cli`: `gen-samples` (seeded, byte-identical; reads the files back and checks counts,
  bbox, enclosed volume and temperatures), `inspect FILE...`, `import-bench` (child process per
  run for clean peak-memory readings; `platform/Process_{win,posix}.cpp`), `--data`, `--sizes`;
  `all` now runs `fea` + `gen-samples`.
- `CsvWriter`: optional shortest round-trip number format (`CsvNumberFormat::Shortest`) for
  human-readable sample data; the evidence CSVs keep `%.17g`.
- Data: `data/geometry/rod_10x10mm_1m.stl` (12 facets), `data/geometry/heatsink_fins.stl`
  (96 facets), `data/thermal/rod_nodal_temperature_SYNTHETIC.csv` (909 nodes) and
  `data/PROVENANCE.md` (generator, seed, units, schema, MIT licence, SYNTHETIC statement).
- Measured limits (`results/benchmarks/import_limits.{md,csv}`): ~250 MB/s STL, ~210 MB/s CSV;
  2 M facets (496 MB) in 2.0 s / 270 MB peak, 5 M nodes (275 MB) in 1.3 s / 318 MB peak.
  `ImportLimits` defaults: 512 MiB, 2,000,000 facets (the spec's 5 M would need a ~1.2 GB file,
  over the byte limit), 5,000,000 nodes, 4,096-character lines.
- Tests: 93 new (176 in total): 37 malformed fixtures in `tests/fixtures/{stl,csv}/` asserting
  the exact `ErrorCode` and line (label `negative`), parser/reader unit tests incl. CRLF + BOM,
  cancellation and limits, `integration.cli_samples_match_data` (regenerated samples equal
  `data/` byte for byte and import cleanly) and `SampleData.*`.
- `.gitattributes`: `tests/fixtures/** -text`, so CRLF, BOM and binary fixtures stay byte-exact.

### Phase 2: Thermal FEA (Task C)
- `uniforge_core` is now a static library. `core/common`: `Result<T>`/`Result<void>`, `Error`
  with `ErrorCode` and `hint` (no exceptions across the core API), `writeFileAtomic` (temp file
  then rename), `CsvWriter` (`%.17g`, ASCII, LF, rejects non-finite values), `SvgPlot`
  (deterministic line / log-log plots, Okabe–Ito colours, dashed reference series, legend
  outside the plot area), `MetaJson` sidecar writer and a portable UTC timestamp formatter.
- `core/thermal`: `RodProblem::validate()`, `Mesh1D`, element matrices, tridiagonal assembly,
  Dirichlet BCs by symmetric elimination (returns the unconstrained rows for reactions),
  Thomas solver with pivot checks, `solveRod` (reentrant, optional reusable `RodWorkspace`),
  analytical solution and peak, `analyse` (nodal and midpoint errors, energy residual),
  `convergenceStudy` with progress and cancellation.
- `uniforge_cli.exe` (`fea`, `all`, `--out`): writes `results/csv/fea_N{10,20,40,80}_{nodes,midpoints}.csv`,
  `convergence.csv`, `.meta.json` sidecars and `results/plots/{T_vs_x,error_vs_h_loglog}.svg`,
  prints the convergence table and PASS/FAIL checks (exit code 3 on a failed check).
- Measured: midpoint errors 1.25, 0.3125, 0.078125, 0.01953125 °C (within 3.6e-14 of q·h²/8k),
  ratio 4, order 2, nodal error ≤ 1.2e-12 °C, energy residual ≤ 8.7e-15 W.
- Tests: 69 new (labels unit, numerical, negative) plus `integration.cli_fea_matches_golden`
  (CLI CSVs = `tests/golden/fea/`, two runs byte-identical); 83 in total.
- Packaging: `app/uniforge_cli.exe` (imports only `KERNEL32.dll`) and root
  `regenerate_evidence.cmd` wrapper; evidence committed under `results/`.
- Docs: `docs/thermal_fea.md` (formulation, verification, debugging log).
- Build: the git-commit lookup moved from `src/app/CMakeLists.txt` to `src/CMakeLists.txt` so
  the CLI shares it; the CLI builds on every platform (`UNIFORGE_BUILD_APP` now covers only the
  GUI and the launcher).

### Phase 1: Walking skeleton and packaging
- Vendored Dear ImGui 1.93.0 (docking), ImPlot 1.1, GoogleTest 1.18.0, Inter 4.1 and
  JetBrains Mono 2.304 with licence texts and pinned commits/checksums in `DEPENDENCIES.md`.
- Patched the ImGui DX11 backend to use fxc-precompiled shaders (`cmake/Shaders.cmake`), so the
  app no longer needs `d3dcompiler_47.dll` at run time.
- Platform layer: Win32 window (per-monitor-v2 DPI, UTF-8 code page, minimum 1024 × 680,
  light/dark title bar) and a Direct3D 11 device that tries hardware, falls back to WARP and
  recovers from `DXGI_ERROR_DEVICE_REMOVED`.
- UI: theme tokens (light/dark, follows Windows app mode), embedded fonts with a checked glyph
  list, AppShell with NavRail (Overview, Geometry, Thermal, Performance, Hardware, Backprop,
  About), StatusBar and LogPanel; task views are placeholders; About shows version, render
  backend and both launch-to-ready numbers.
- Services: `JobRunner` (one job at a time, atomic progress + stage, `CancelToken`, publish
  only on success), `ResultStore`, `LogSink`; Overview demo job exercises progress and cancel.
- `Launch.exe`: starts `app\UniForge.exe --launch-t0=<QPC>` from the package folder; error dialog
  with path, system error and fix hint.
- Read-only package folder: logs and `imgui.ini` fall back to `%LOCALAPPDATA%\UniForge\`.
- `tools/package.ps1` (build, test, stage, dependency check, hygiene scan, zip),
  `tools/hygiene_scan.ps1`, ZIP-specific VS Code config, repo VS Code config.
- CI: `windows-2022` job (configure, build Release with `/W4 /WX`, ctest, package, upload ZIP).
- Tests (label `unit`): JobRunner progress, cancel keeps previous result, busy rejection,
  failure containment, shutdown; demo-job cancel latency < 200 ms; LogSink format and file
  output; font glyph coverage.
- `.clang-format`: `<windows.h>` sorts before other Win32 headers; CMake MSVC options apply
  to C++ sources only (not resource scripts).

### Phase 0: Setup
- Repository conventions: licence, `.gitignore`, `.gitattributes`, `.editorconfig`.
- CMake skeleton with presets for MSVC (Windows) and GCC/Clang (Linux, incl. sanitizer builds).
- Shared compiler options: C++17, warnings, static MSVC runtime, optional sanitizers.
- Formatting and static-analysis configuration (`.clang-format`, `.clang-tidy`).
- Design documents, AI-use log and effort log.
