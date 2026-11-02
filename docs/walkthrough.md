# Walkthrough: where is X, how do I change Y, how do I rerun Z

A map of the code for the verification discussion. Paths are relative to the repository root.

## Layers

```text
src/core/      portable C++17, standard library only (builds on Windows and Linux)
  common/      Result<T> + ErrorCode, CancelToken, Progress, AtomicFile, CSV/SVG/meta.json writers, Rng
  thermal/     Task C: mesh, element matrices, assembler, Dirichlet BCs, Thomas solver, analytics
  io/          Task B: LineReader, STL and nodal-CSV parsers, ImportLimits, error messages
  perf/        Task D: batch generator, WorkerPool, serial/parallel runners, benchmark, SystemInfo
  hw/          Task E: device interface, mock transport, adapters, state machine, RetryPolicy
  ml/          Task F: one-neuron network, backward pass, gradient check, trainer
src/app/       Windows UI (Dear ImGui + ImPlot on Win32 / Direct3D 11, WARP fallback)
  platform/    window, D3D11 device, paths, launch timing, crash handler, file dialog, capture
  services/    JobRunner (one background job), ResultStore (atomic publish), LogSink, DeviceManager
  render/      3D viewport: camera, picking, mesh and point renderers, PNG writer
  ui/          theme tokens, fonts, number formatting, widgets (badges, run bar, tables, ...)
  views/       Overview, Geometry, Thermal, Performance, Hardware, Backprop, About
src/cli/       uniforge_cli: fea, gen-samples, inspect, import-bench, bench, backprop, all
src/launcher/  Launch.exe: starts app\UniForge.exe with --launch-t0, shows an error dialog if it fails
```

Dependencies only point downwards: views → services → core. Nothing in `core/` includes a
Windows or UI header, which is why the Linux CI jobs can build and test it under ASan/TSan.

## Where is ...

| Question | Answer |
|---|---|
| The FEA stiffness matrix and load vector | `src/core/thermal/ElementMatrices.hpp` (k/h·[1 −1; −1 1], qh/2·[1 1]), assembled in `Assembler.cpp` |
| How the boundary conditions are applied | `DirichletBC.cpp`: symmetric elimination (move known T0/TL to the right-hand side, keep the matrix SPD and tridiagonal) |
| The linear solver | `ThomasSolver.cpp`: forward sweep + back substitution, no pivoting (matrix is diagonally dominant) |
| Why the midpoint error is exactly qh²/8k | `docs/thermal_fea.md` §3; checked in `tests/numerical/thermal/ConvergenceStudyTest.cpp` |
| How a bad STL line is reported | `src/core/io/StlParser.cpp` returns `Result` with `ErrorCode`, line number and hint; texts in `ImportErrors.cpp` |
| The import size limits | `src/core/io/ImportLimits.hpp`; measured in `results/benchmarks/import_limits.md` |
| How the thread pool splits work | `src/core/perf/WorkerPool.cpp`: atomic chunk index, chunk = max(1, cases/(8p)) |
| Why serial and parallel results are bitwise equal | both call the same `solveCase` and write `out[i]` for case `i` (`SerialRunner.cpp`, `ParallelRunner.cpp`) |
| The retry/back-off rule | `src/core/hw/RetryPolicy.cpp`: 250 ms timeout, 3 retries at 100/200/400 ms |
| The device state machine | `src/core/hw/DeviceStateMachine.cpp` (Disconnected → Connecting → Connected → Faulted → Recovering) |
| The backprop formulas | `src/core/ml/TinyNet.cpp` (`forward`, `backward`); derivation in `docs/backprop.md` and the handwritten appendix |
| How a background job keeps the UI responsive | `src/app/services/JobRunner.cpp`: one worker thread, progress + `CancelToken`, result published by `shared_ptr` swap in `ResultStore` |
| Why a cancelled run keeps the old result | the job only calls `ResultStore::publish` on success |
| Launch-to-ready measurement | `src/launcher/Launch.cpp` passes QPC; `src/app/platform/LaunchTiming.cpp` measures at the first Present |
| What happens on a crash | `src/app/platform/CrashHandler.cpp` writes `logs/crash_<pid>.txt` and shows a message box |
| Read-only install folder | `src/app/platform/Paths.cpp`: probe write; fall back to `%LOCALAPPDATA%\UniForge` |

## How do I change ...

| Change | Where | Then |
|---|---|---|
| The rod parameters used for the evidence | `src/core/thermal/RodProblem.hpp` (brief defaults) | `uniforge_cli fea`, update `tests/golden/fea/` |
| The mesh sizes in the study | `src/cli/commands/FeaCommand.cpp`, Thermal view checkboxes in `ThermalView.cpp` | rerun the study |
| An import limit | `src/core/io/ImportLimits.hpp` | `ctest --preset windows-release -L negative` |
| The benchmark workload | `reportPreset()` in `src/core/perf/Benchmark.cpp` | `uniforge_cli bench --report` |
| The retry timing | `src/core/hw/RetryPolicy.hpp` | `ctest --preset windows-release -R HwRetry` |
| The learning rate / steps | `BackpropProblem` in `src/core/ml/TinyNet.hpp` | `uniforge_cli backprop`, update `tests/golden/backprop/` |
| A colour or spacing | `src/app/ui/Theme.cpp` (tokens), never in a view | rebuild |
| Add a view | implement `views::View`, register it in `src/app/ui/AppShell.cpp` | rebuild |

## How do I rerun ...

```text
:: everything, from a fresh clone (Developer PowerShell for VS 2022)
cmake --preset windows-msvc
cmake --build --preset windows-release
ctest --preset windows-release            :: 289 tests
ctest --preset windows-release -L numerical
ctest --preset windows-release -R ConvergenceStudy

:: the evidence (from the package)
regenerate_evidence.cmd                         :: = app\uniforge_cli.exe all
app\uniforge_cli.exe fea --out results
app\uniforge_cli.exe bench --report
app\uniforge_cli.exe backprop

:: packaging and checks
powershell -ExecutionPolicy Bypass -File tools\package.ps1
powershell -ExecutionPolicy Bypass -File tools\edge_case_runs.ps1
```

## Numbers to know

| Quantity | Value | Source |
|---|---|---|
| Max midpoint error, N = 10/20/40/80 | 1.25, 0.3125, 0.078125, 0.01953125 °C (= qh²/8k), order 2.0000 | `results/csv/convergence.csv` |
| Nodal error | ≤ 1.3e-12 °C (linear elements are exact at nodes in 1D) | same |
| Peak temperature | 188.2 °C at x = 0.42 m | `docs/thermal_fea.md` |
| Benchmark 10 000 × 8 000 | T1 = 1.073 s; speedup 2.00 (p = 2), 4.00 (p = 4); max \|ΔT\| = 0 | `docs/performance.md` |
| Import throughput | ~250 MB/s STL, ~210 MB/s CSV; 2 M facets in 2.0 s | `results/benchmarks/import_limits.md` |
| Launch-to-ready | 0.12–0.13 s from Launch.exe | `results/edge_cases.md`, app log |
| Backprop | gradients match central differences to ~1e-10; J falls from 1.18 to 1.7e-20 in 100 steps | `results/csv/backprop_gradients.csv`, `loss_curve.csv` |
