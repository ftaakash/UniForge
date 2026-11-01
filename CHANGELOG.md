# Changelog

All notable changes are recorded here, one section per development phase.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

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
