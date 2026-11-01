# Changelog

All notable changes are recorded here, one section per development phase.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

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
