# Changelog

All notable changes are recorded here, one section per development phase.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

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
