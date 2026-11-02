# AI-Use Log

The brief allows any AI assistant but requires disclosure: tools and models, representative
prompts, relevant outputs, my edits, how I verified the output, and at least one incorrect or
unsuitable suggestion (or a deliberate challenge test). This log is kept **during** development,
one entry per working session. Nothing here is reconstructed after the fact.

## Tools

| Tool | Model | Used for |
|---|---|---|
| Claude Code (Claude desktop app, Code tab) | Claude Opus 5.5 | Requirements analysis, design documents, code generation, test writing, review |

Commits are made under my own git identity. Where AI produced code or text, it is recorded here
and summarised in [ownership.md](ownership.md) (added in Phase 9).

---

## 2026-11-01: Requirements analysis and design (Phase 0)

### Prompts (representative)
1. "Analyze the two documents [assignment brief PDF + my workflow notes], create your analysis and
   development plan, but don't start executing it yet … we need above 95 out of 100."
2. `/engineering:system-design`: "create a complete system design plan for this project to maximize
   the score … we compare it with our existing plan then move forward with the best."
3. `/design:design-system`: "check the design plan above, make corrections if any needed and create
   your version of design."
4. "Yes, merge them": merge all three documents into one build specification.

### Outputs
- `docs/design/UniForge_Analysis_and_Development_Plan.md`: rubric strategy, stack, phases.
- `docs/design/UniForge_System_Design.md`: requirement IDs, contracts, threading, NFRs.
- `docs/design/UniForge_UI_Design_System.md`: review of the first two documents + UI specification.
- `docs/design/UniForge_Final_Build_Spec.md`: the merged specification used for the build.

### Problems found in AI output (review step 3 checked steps 1–2)
The review pass found 12 issues in the earlier AI-generated designs. The most significant:

| # | Unsuitable suggestion | Why it was wrong | Resolution |
|---|---|---|---|
| 1 | VS Code launch config pointing at `dist/…/UniForge.exe` | That folder does not exist inside the extracted ZIP, so the required one-action run config would fail for the evaluator | ZIP gets its own config targeting `Launch.exe` |
| 2 | No font specified (ImGui default font) | The default font has no glyphs for °, ², ·, ε, so every unit label would render as `?` | Embed Inter + JetBrains Mono with explicit glyph ranges |
| 3 | Launch-to-ready measured from `UniForge.exe` start | Misses time spent in `Launch.exe`, which is what the user double-clicks | Launcher passes its start timestamp to the app |
| 4 | "MSVC only supports OpenMP 2.0" as the reason to avoid OpenMP | Incomplete: `/openmp:llvm` exists; the real issue is the extra runtime DLL | Rationale corrected |
| 5 | "Reduced K is diagonally dominant" | Interior rows are only weakly dominant; the correct property is *irreducibly* diagonally dominant | Wording corrected |

The full list of 12 is in `UniForge_UI_Design_System.md` Part 1 and its application in
`UniForge_Final_Build_Spec.md` §17.

### Verification
- Analytical targets from the brief were re-derived independently of the workflow notes:
  `T_exact = 100 + 420x − 500x²`, maximum 188.2 °C at x = 0.42 m, midpoint error `qh²/8k`
  (1.25, 0.3125, 0.078125, 0.01953125 °C); backprop forward/gradient/update values match the
  brief's tables. These become automated tests in Phases 2 and 7.
- Environment facts used in the plan (no C++ toolchain installed, CPU/RAM, empty repo) were
  checked with commands rather than assumed.

### My decisions
- Chose to generate two further independent designs and compare them before building.
- Approved the merge and the adopted defaults (Overview view, theme follows Windows,
  Backprop as a fifth view, video demo).
- Set commit rules: commits under my own identity, phase-by-phase history.

---

## 2026-11-01: Walking skeleton and packaging (Phase 1)

### Prompts (representative)
1. "Implement Phase 1 (walking skeleton + packaging) of UniForge … following
   `docs/design/UniForge_Final_Build_Spec.md` … Do not add features from later phases." The brief
   listed the scope (vendoring, Win32/D3D11 with WARP fallback, theme/fonts/AppShell, JobRunner,
   Launch.exe, read-only fallback, `package.ps1`, CI, JobRunner tests, docs) and the done criteria.
2. Commit rules: nothing is committed until I have reviewed the diff.

### Outputs
- `third_party/` (ImGui 1.93.0-docking, ImPlot 1.1, GoogleTest 1.18.0, Inter, JetBrains Mono),
  `LICENSES/`, `DEPENDENCIES.md`, `third_party/patches/imgui_impl_dx11_precompiled_shaders.patch`.
- `src/app/platform/` (Win32Window, D3D11Context, Paths, LaunchTiming, SystemTheme, Resources, Text),
  `src/app/ui/` (Theme, Fonts, GlyphRanges, AppShell, widgets), `src/app/views/`,
  `src/app/services/` (JobRunner, ResultStore, LogSink, DemoJob), `src/core/common/`
  (CancelToken, Progress), `src/launcher/Launch.cpp`.
- `tools/package.ps1`, `tools/hygiene_scan.ps1`, `.vscode/`, `tools/package/vscode/`,
  `.github/workflows/ci.yml`, tests under `tests/unit/`.

### Verification
- Release build with `/W4 /WX`: no warnings. `ctest --preset windows-release`: 13/13 pass; the
  JobRunner suite was also run 20 times in a row (`--repeat until-fail:20`) to look for races.
- `tools/package.ps1`: `dumpbin /dependents` shows only Windows DLLs (`Launch.exe`: KERNEL32,
  USER32; `UniForge.exe`: KERNEL32, USER32, GDI32, ADVAPI32, SHELL32, ole32, IMM32, d3d11,
  dwmapi; no VC++ runtime, no `d3dcompiler_47.dll`). Hygiene scan clean; ZIP 3.2 MB.
- The hygiene scan was tested on a planted file with a profile path, an e-mail address and an
  `api_key = "…"` line: all three were reported and the script exited 1.
- Extracted the ZIP to a folder whose path contains spaces, `ü`, `ï` and CJK characters and
  started `Launch.exe`: launch-to-ready 0.139 s from Launch.exe, 0.085 s from UniForge.exe
  (`logs/launch.log`).
- `--force-warp`: WARP device ("Microsoft Basic Render Driver"), status-bar warning shown.
- Read-only package folder (write denied with `icacls`): logs went to `%LOCALAPPDATA%\UniForge`,
  the warning was logged and shown in the status bar.
- Renamed `app\` and started `Launch.exe`: the dialog shows the path tried, "The system cannot
  find the path specified. (error 3)" and the re-extract hint. The non-zero exit code is from
  reading the code; I could not close the dialog from a script to observe it.
- Demo job, driven by keyboard and checked with window screenshots: progress and stage update
  while the UI keeps rendering; cancel stopped the job 13 ms after the request (log); a run that
  completed (run #2) stayed the published result after the next run (#3) was cancelled.
- Windows Sandbox (GPU disabled, no VC++ runtime), ZIP extracted to `Desktop\UniForge test ünï`:
  started with no SmartScreen prompt, ready in 0.090 s from Launch.exe (0.070 s from UniForge.exe);
  every view opened; demo run completed in 2.22 s; the next run was cancelled 26 ms after the
  request and the completed result stayed. Screenshots and `launch.log` in `docs/demo/phase1/`.
  This run surfaced finding 7 below.
- Not verified: DPI change between monitors (single monitor); the finding-7 fix in a no-GPU
  Sandbox (a later Sandbox session passed the real GPU through despite `VGpu=Disable`, and my
  forced restarts of the Sandbox left a broken session that logged off).

### Incorrect / unsuitable suggestions and how they were caught
| # | What the AI produced | Problem | How it was caught | Resolution |
|---|---|---|---|---|
| 1 | `JobRunner::finish()` marked the runner idle, then wrote the job's final log line | A thread that saw "idle" (UI, tests, shutdown) could read the log before the line existed | New test `JobRunner.ExceptionFailsJobAndKeepsPreviousResult` failed | Log first, then publish the idle state |
| 2 | Device-loss recovery called only `ImGui_ImplDX11_Shutdown()` + `Init()` | In the docking branch the DX11 shutdown calls `DestroyPlatformWindows()`, which also frees the Win32 backend's main-viewport data; the next frame crashed with an access violation | Running the packaged app with `--simulate-device-lost` (Windows Application log: 0xc0000005) | Restart both backends (Win32 + DX11) around the device re-creation; re-tested in Debug and Release |
| 3 | Keep the stock ImGui DX11 backend: `d3dcompiler_47.dll` ships with Windows, so the DLL check would still pass | Contradicts spec decision D3 (shaders precompiled with fxc) and keeps a run-time shader compiler dependency | My review against the spec | Rejected: small documented patch + fxc build step |
| 4 | Spec §8.2 wording "glyph ranges … atlas rebuilt on DPI change" applied literally | Since ImGui 1.92 glyphs are rasterised on demand and `GlyphRanges` is ignored by modern backends, so the literal implementation would do nothing | Reading the vendored `imgui_draw.cpp` | Glyph list kept as a contract, checked at start-up and by a unit test against the TTF files; DPI change compacts the glyph cache |
| 5 | clang-format run with the Phase 0 config | `IncludeBlocks: Regroup` sorted `<shellapi.h>`/`<shlobj.h>` before `<windows.h>` | Reading the reformatted includes before building | Added `IncludeCategories` so `<windows.h>` sorts first |
| 7 | Software rendering detected only when the *hardware* device request failed | In Windows Sandbox with no GPU driver the hardware request succeeds on the "Microsoft Basic Render Driver" (software), so the app showed "D3D11 HW" and no "Software rendering" warning | Windows Sandbox clean-machine run (GPU disabled), log line `render backend: D3D11 HW · Microsoft Basic Render Driver` | The adapter is now checked (`DXGI_ADAPTER_FLAG_SOFTWARE`, or vendor 0x1414 / device 0x8C) and reported as software rendering. Re-test in a no-GPU Sandbox still pending: the second Sandbox session ignored `VGpu=Disable` and got the real GPU |
| 6 | First hygiene-scan path rule | Flagged the bare example "`C:\Users`" quoted in a Phase 0 design doc | Package run failed on it | Rule now needs two path segments (any real profile path still matches); design doc left unchanged as evidence |

Two edits made with shell `sed`/`perl` one-liners corrupted files (a stray control character, a
misplaced function); the build or a parse error caught both, and the files were rewritten.

### My decisions
- Installed only the VS 2022 Build Tools (no full IDE) to keep the toolchain small. The quiet
  install hung, so I had it stopped and reinstalled with `--passive`.
- Kept the packaged layout: `Launch.exe` plus `app\`, static CRT, no installer.
- Reviewed Phase 1 before the push. Every later phase follows the same rule.

---

## 2026-11-01: Thermal FEA core (Phase 2)

### Prompts (representative)
1. "Implement Phase 2 (Thermal FEA core, Task C) of UniForge in this repo, following
   `docs/design/UniForge_Final_Build_Spec.md` … Don't build later-phase features." The brief
   listed the scope (Result/Error, CSV/SVG/meta writers, the thermal modules, tests with the
   exact expected values, `uniforge_cli fea`/`all`, golden-file integration test, packaging,
   committed evidence, `docs/thermal_fea.md` with a debugging log that invents nothing).
2. Commit rules: nothing is committed until I have reviewed the diff; no AI attribution lines.

### Outputs
- `src/core/common/`: `Error.hpp`, `Result.hpp`, `AtomicFile`, `CsvWriter`, `SvgPlot`, `MetaJson`.
- `src/core/thermal/`: `RodProblem`, `Mesh1D`, `ElementMatrices`, `Assembler`, `DirichletBC`,
  `ThomasSolver`, `Analytical`, `RodSolver`, `ErrorAnalysis`, `ConvergenceStudy`.
- `src/cli/` (`uniforge_cli.exe`), `tools/package/regenerate_evidence.cmd`, `tools/package.ps1` changes.
- Tests: `tests/unit/common/`, `tests/unit/thermal/`, `tests/numerical/thermal/`,
  `tests/negative/thermal/`, `tests/support/DenseGauss.hpp` (test-only oracle),
  `tests/integration/cli_golden.cmake`, `tests/golden/fea/`.
- `results/csv/`, `results/plots/`, `docs/thermal_fea.md`, README/CHANGELOG/DEPENDENCIES updates.

### Verification
- Release build with `/W4 /WX`: no warnings. `ctest --preset windows-release`: 83/83 pass
  (13 from Phase 1, 69 new unit/numerical/negative tests, 1 integration test).
- Measured values (`results/csv/convergence.csv`): midpoint errors 1.2500000000000284,
  0.3125, 0.078124999999964473, 0.019531249999992895 °C vs q·h²/8k = 1.25, 0.3125, 0.078125,
  0.01953125; ratio 4 and order 2 to within 7e-13; max nodal error 1.2e-12 °C; energy
  residual ≤ 8.7e-15 W; exact peak 188.2 °C at 0.42 m (FE node 21 of N = 50 and node 42 of
  N = 100 hit it to 1e-10).
- Thomas vs the dense Gaussian-elimination oracle: agree to 1e-11 °C on the rod systems and to
  1e-12 (relative) on 20 random diagonally dominant systems.
- Determinism: the integration test runs `fea` and `all` into two folders and compares every
  CSV with `tests/golden/fea/` and every CSV/SVG between the runs. Changing the last digit of
  one golden value made it fail ("differs from golden: convergence.csv").
- Fault injection (deliberate, reverted): removing `f[1] −= a[1]·T0` from the Dirichlet
  elimination failed 18 tests and all but one CLI check; details in `docs/thermal_fea.md` §5.
- `tools/package.ps1`: `uniforge_cli.exe` imports only `KERNEL32.dll`; hygiene scan clean;
  ZIP 4.3 MB. Extracted the ZIP to a folder whose path has spaces, `ü` and `ï`, deleted its
  `results/`, ran `regenerate_evidence.cmd`: all 11 regenerated CSV/SVG files had the same
  SHA-256 as the committed ones.
- Both SVG plots inspected visually in a browser.
- Not verified yet: the Linux/GCC build of the new core and CLI (Linux CI arrives in Phase 5).
  The code avoids MSVC-only constructs and was written for `-Wconversion -Wsign-conversion
  -Wshadow`, but it has not been compiled with GCC.

### Incorrect / unsuitable suggestions and how they were caught
| # | What the AI produced | Problem | How it was caught | Resolution |
|---|---|---|---|---|
| 1 | `src/cli/cli.manifest` with a comment that mentioned the `--out` option | XML forbids `--` inside comments; the linker's manifest tool failed (`c1010070`) | First build of `uniforge_cli` | Comment reworded |
| 2 | `T_vs_x.svg` drawing the meshes coarse to fine | All four meshes lie on the same curve (nodal exactness), so the 81 N = 80 markers hid the other three series | Looking at the rendered SVG | Finest mesh drawn first, coarse ones on top |

### Deviations from the spec for me to confirm
- `analyse()` returns `Result<ErrorReport>` instead of `ErrorReport` (spec §5.2), so a solution
  that does not match the problem gives `InvalidInput` instead of undefined behaviour (spec
  §5.2 also says violations → `Error`, never UB). `ErrorReport` gained `nodalExact` for the CSVs.
- `convergenceStudy` takes an optional `std::vector<StudyMesh>*` so the CLI gets each mesh's
  solution without solving twice, and requires strictly increasing N.
- `solveRod` has a second overload with a reusable `RodWorkspace` for the Phase 5 batch runner.
- Element-matrix and assembler tests carry the `unit` label (spec §10 lists them as unit tests);
  the task brief had them under `numerical/`.
- `uniforge_cli` is built on every platform, not only with `UNIFORGE_BUILD_APP`, so the
  golden-file test can run on Linux in Phase 5.

### My decisions
- Accepted the deviations above. `Result<ErrorReport>` is stricter than the spec and keeps the
  "errors, never UB" rule.
- Moved Phase 2 into a separate session so Phase 1 could be reviewed independently.

---

## 2026-11-01: Importers, Task B data side (Phase 3)

### Prompts (representative)
1. "Implement Phase 3 (importers, Task B data side) of UniForge in this repo, following
   `docs/design/UniForge_Final_Build_Spec.md` … No rendering or Geometry view UI (that's
   Phase 4)." The brief listed the scope: streamed `LineReader`, `ImportLimits`, the STL and CSV
   parsers with every rejection case, ~15 STL + ~12 CSV negative fixtures asserting code and
   line, positive tests (CRLF + BOM, cancellation, limits), `gen-samples` with golden copies,
   `data/PROVENANCE.md` stating the CSV is SYNTHETIC, an `import-bench` command measuring
   10 k / 100 k / 1 M elements, limits set from the measurements, and doc updates.
2. Commit rules as before: nothing committed before my review; no AI attribution lines.

### Outputs
- `src/core/io/`: `LineReader`, `TextParse` (`from_chars` wrappers, message helpers),
  `ImportTypes`, `ImportLimits`, `ImportErrors`, `StlParser`, `ThermalCsvParser`, `StlWriter`.
- `src/core/common/CsvWriter`: optional shortest number format (`CsvNumberFormat::Shortest`).
- `src/cli/`: `SamplesCommand` (`gen-samples`, `inspect`), `ImportBenchCommand` (`import-bench`
  + internal worker), `platform/Process.hpp` with `Process_win.cpp` / `Process_posix.cpp`,
  `--data` / `--sizes` options, `all` = `fea` + `gen-samples`.
- Tests: `tests/unit/io/` (4 files), `tests/negative/io/ImportFixtureTest.cpp`,
  `tests/fixtures/stl/` (21 malformed + 1 valid), `tests/fixtures/csv/` (16 malformed + 1 valid),
  `tests/integration/samples_golden.cmake`, `tests/integration/SampleDataTest.cpp`.
- `data/` (3 sample files + `PROVENANCE.md`), `results/benchmarks/import_limits.{md,csv,meta.json}`,
  README / CHANGELOG / `.gitattributes` / `regenerate_evidence.cmd` comment updates.

### Verification
- Release build with `/W4 /WX`: no warnings. `ctest --preset windows-release`: 176/176 pass
  (83 existing + 93 new: 43 `unit_io`, 45 `negative_io`, 1 `CsvWriter`, 3 `SampleData`,
  1 `integration.cli_samples_match_data`).
- Ran `uniforge_cli inspect` over every fixture and read all 37 rejection messages and hints for wording
  and line numbers (three were reworded: plurals, the empty-value hint, CSV field names).
- Fault injection (deliberate, reverted): changing one digit of the committed SYNTHETIC CSV
  failed `integration.cli_samples_match_data` ("differs from the committed copy"); making
  `parseDouble` accept NaN/Inf failed 6 tests (1 unit, 5 negative fixtures).
- `gen-samples` checks pass: rod 12 facets, bbox exactly [0, 1] × [−0.005, 0.005]² m, enclosed
  volume = A·L = 1e-4 m³; heat sink 96 facets, volume 2.416e-5 m³; CSV 909 unique nodes, flagged
  SYNTHETIC, every node within 0.02 °C (+ rounding) of the documented formula, Tmax 188.1923 °C.
- `import-bench` (i5-14600K, 32 GB, MSVC 19.44 Release): ~250 MB/s STL, ~210 MB/s CSV;
  1 M facets 248 MB / 1.0 s / 123 MB peak; 2 M facets 496 MB / 2.0 s / 270 MB peak;
  1 M nodes 54 MB / 0.24 s / 68 MB peak; 5 M nodes 275 MB / 1.3 s / 318 MB peak. Temporary files
  were deleted (checked the temp folder afterwards). Largest file on disk at once: 496 MB.
- `tools/package.ps1`: all tests pass, `uniforge_cli.exe` still imports only `KERNEL32.dll`
  (`K32GetProcessMemoryInfo`), hygiene scan clean, ZIP 4.6 MB. In the staged package, deleting
  `data/geometry` and `data/thermal` and running `app\uniforge_cli.exe all` recreated all three
  samples byte-identical to the repository copies.
- `TextParse.IgnoresTheCLocaleDecimalSeparator` switched the C locale to German (it exists on
  this machine) and `from_chars` still parsed `0.25` and rejected `0,25`.
- Not verified yet: the Linux/GCC build, including `Process_posix.cpp` (Linux CI arrives in
  Phase 5; floating-point `from_chars` needs GCC ≥ 11). `GetProcessMemoryInfo`'s peak values
  were taken at face value; they were not cross-checked with another tool.

### Incorrect / unsuitable suggestions and how they were caught
| # | What the AI produced | Problem | How it was caught | Resolution |
|---|---|---|---|---|
| 1 | Spec default `maxFacets = 5'000'000` with `maxBytes = 512 MiB` (from the design phase) | The two limits contradict each other: 5 M ASCII facets is a ~1.24 GB file, so the facet limit could never be reached and was never testable | First `import-bench` run (248 MB per 1 M facets) | `maxFacets = 2'000'000` (496 MB file), and the bench now also runs exactly at each limit |
| 2 | Sample temperatures rounded as `round(T / 1e-4) * 1e-4` | Multiplying by the inexact `1e-4` gave values such as `99.76350000000001` in the CSV | Reading the generated file | `round(T * 1e4) / 1e4`, the nearest double to the 4-decimal value |
| 3 | A shell `sed` edit of `regenerate_evidence.cmd` | Backslash escaping turned `app\uniforge_cli.exe` into `appNiforge_cli.exe` in a comment | Reading the file after the edit | Fixed with an exact-text edit |
| 4 | Running `clang-format` on the new platform files, then an `awk` one-liner to restore the include order | clang-format moved `Process.hpp` below `<windows.h>`; the `awk` script then deleted every blank line in `Process_win.cpp` | Reading both files | File rewritten; include order matches the rest of the repository |
| 5 | First fault-injection attempt (`if (false && …)`) | Did not compile under `/WX` (constant condition), so it tested nothing | Build output | Replaced with a mutation that compiles |
| 6 | A round-trip test value `4.9e-324` (subnormal) | libstdc++'s `from_chars` may report `result_out_of_range` for subnormals (not tried on Linux); a portability risk, not an observed failure | Review before running | Replaced with `DBL_MIN` (2.2250738585072014e-308) |

### Deviations from the spec for me to confirm
- `ImportLimits` gained `maxLineBytes` (4096); `maxFacets` is 2,000,000 instead of 5,000,000
  (see #1 above).
- `StlMesh` gained `recomputedNormals`. Types live in `namespace uniforge::io`.
- `parseAsciiStlFile` / `parseThermalCsvFile` overloads open the file and set the size hint;
  `syntheticFlag` is set only by the file overload (the stream overload has no name).
- Lenient where real exporters differ: keywords in any case, a leading `+` on numbers, several
  solids merged, a missing `endsolid` after complete facets is a warning (EOF inside a facet is
  still an error).
- Codes chosen where the spec names none: duplicate `node_id` and T < −273.15 °C are
  `InvalidInput`; a number outside the double range (overflow or underflow, e.g. `1e400`,
  `1e-400`) is `NonFinite`; a file not starting with `solid` is `UnsupportedFormat` with line 1.
- `all` now runs `fea` + `gen-samples`; `import-bench` stays separate because its numbers are
  machine-specific and it writes ~0.5 GB of temporary files.
- `CsvWriter` (Phase 2 code) gained a shortest-format option for the sample CSV; the Phase 2
  evidence files and their golden copies are unchanged.
- The heat sink is 8 separate closed boxes (touching), not one fused solid.

### My decisions
- Accepted the 2,000,000-facet limit and the lenient parsing rules above. Real exporters
  differ, so a strict parser would reject valid files.
- Kept `import-bench` out of `all` because its numbers depend on the machine.

---

## 2026-11-02: Visual views, Tasks B and C UI (Phase 4)

### Prompts (representative)
1. "Implement Phase 4 (visual views) of UniForge, following
   `docs/design/UniForge_Final_Build_Spec.md` … render module (Camera, MeshRenderer,
   PointCloudRenderer, Picking, axes triad, offscreen target, MSAA 4× if supported, works on
   WARP), Geometry view, Thermal view, Overview live cards, widgets from §8.4, `ui/Format.hpp`
   with unit tests, tests for camera fit and picking, manual checklist + screenshots."
2. Parallel-work rules for the night: own branch and worktree, small additive edits to shared
   files, commit per scope without waiting for review, no push/tag/merge, no AI attribution.

### Outputs
- `src/app/render/`: `RenderMath.hpp`, `Camera` (orbit / pan / zoom-to-cursor / fit / reset),
  `Picking` (CPU screen-space nearest within 6 px, depth tie-break), `SceneRenderer` (offscreen
  MSAA target resolved for `ImGui::Image`, flat-shaded mesh + wireframe pass, instanced point
  quads with an inferno 1D texture), `PngWriter` (stored-deflate PNG, no dependency).
- `src/app/shaders/scene_{mesh,points}_{vs,ps}.hlsl` (fxc via `cmake/Shaders.cmake`).
- `src/app/services/`: `GeometryModel` (import → recentred float arrays, UI-less),
  `ThermalStudy` (study job + three CSV exports); `ResultStore` slots `geometryMesh`,
  `geometryPoints`, `thermal`.
- `src/app/platform/`: `FileDialog` (IFileOpenDialog / IFileSaveDialog), `ScreenCapture`
  (back-buffer region → PNG between render and Present).
- `src/app/ui/`: `Format` (spec §8.3), `ThemeViz` (series colours, mesh/pick colours, inferno),
  widgets `Forms` (VerificationBadge, InlineAlert, ParamField, FileField, RunBar), `DataViews`
  (DataTable, ExportMenu, ColorLegend, NodeInspector), `Viewport3D`.
- `src/app/views/GeometryView`, `ThermalView`; Overview rows A–F with live status for A, B, C.
- Small additive edits to `App.cpp/.hpp` (drag-drop, capture hook, device generation),
  `AppContext.hpp`, `AppShell.cpp`, CMake lists.
- Tests: `tests/unit/ui/FormatTest.cpp`, `tests/unit/render/{Camera,Picking,PngWriter}Test.cpp`,
  `tests/integration/GeometryPipelineTest.cpp`.

### Verification
- Release build with `/W4 /WX`: no warnings. `ctest --preset windows-release`: 210/210 pass
  (176 existing + 34 new: 10 Format, 7 Camera, 6 Picking, 3 PngWriter, 8 pipeline/study).
- Camera tests check maths, not only "no crash": all 8 bbox corners on screen after fit, the
  target projects to the viewport centre after orbit, a 40 px pan moves the anchor exactly
  40 px, and the world point under the cursor stays at the same pixel after a zoom.
- Pipeline test: a 10 µm triangle 1 km from the origin keeps its size in float only because
  of the recentring (it would collapse without it).
- `tools/package.ps1 -SkipBuild` passes: UniForge.exe imports only OS DLLs, hygiene clean,
  ZIP 4.9 MB, `data/` in the package (needed by "Load samples").
- App started with `--force-warp --theme=light`: log shows the WARP backend and launch-to-ready
  0.047 s, no errors. **Not done:** interactive check and screenshots: the request to control
  the app window was declined in this session, so the 3D view, picking, plots and exports have
  not been looked at on screen yet (checklist in `docs/demo/phase4/README.md`).

### Incorrect / unsuitable suggestions and how they were caught
| # | Suggestion / action | Problem | How caught | Resolution |
|---|---|---|---|---|
| 1 | Running the MSBuild step through Git Bash with `/m /v:minimal` | MSYS path conversion turned `/m` into `M:/`, so MSBuild only printed its usage text; the "build" built nothing | Reading the log tail (`Switch: M:/`) | Builds run from PowerShell |
| 2 | A `perl` one-liner inserting `std::wstring path(len, L'\0')` into `App.cpp`, then a `sed` "fix" | perl dropped the escape (empty character constant, C2137); the sed replacement used `\0`, which sed reads as "the whole match", and pasted the line into itself three times | Compiler error, then reading the file | Exact-text edit; duplicate block deleted |
| 3 | `clang-format` over `App.hpp` with `#include <shellapi.h>` | It sorted `<shellapi.h>` into the standard-header block above `<windows.h>`; with lean `windows.h` that breaks `HDROP` | Reading the diff before building | `App.hpp` takes the `WPARAM`; only `App.cpp` includes `shellapi.h` |
| 4 | One large bash heredoc writing five test files | Shell quote mismatch: nothing was written, but the command looked like it might have | `ls` of the test folders | Files written one by one |

### Deviations from the spec for me to confirm
- "Run all self-checks" on Overview not built (cut-list item 1). 200 % DPI not checked.
- The "Non-finite rejected" badge passes when an import succeeded (every value was checked
  finite while parsing) or when the last import was rejected as `NonFinite`; otherwise pending.
- Plot PNG export captures the plot's screen region from the back buffer (exact on-screen look,
  current window size); PNGs are uncompressed-deflate, so a few MB.
- Thermal CSV exports from the UI are long tables over all N (`N,x_m,…`); the per-N evidence files
  stay with `uniforge_cli fea`.
- The camera is z-up (CAD convention); the inferno colours come from a CC0 polynomial fit, not
  the matplotlib table.
- `AppContext` gained `gfx`, `window`, `droppedFiles`; `RuntimeInfo` gained `deviceGeneration`.

### My decisions
- Cut "Run all self-checks" from Overview. It is cut-list item 1, and the deadline matters
  more.
- While clicking through the packaged app I found that the Thermal run bar showed a blank blue
  bar after a study. The progress bar was wider than the panel, which clipped its "100 %" label.
  I had it fixed before the merge (see the integration entry).

---

## 2026-11-02: Multicore benchmark, Task D (Phase 5)

### Prompts (representative)
1. "Implement Phase 5 (multicore, Task D) of UniForge, following
   `docs/design/UniForge_Final_Build_Spec.md` …" with the scope: portable `Rng`, `core/perf`
   (batch generator, bounded worker pool, serial/parallel runners, benchmark, system info),
   `uniforge_cli bench` with Quick/Report presets, the Performance view, tests, Linux sanitizer
   CI jobs, `docs/performance.md` with provisional numbers and a one-command regeneration.
2. Run in parallel with Phases 4, 6 and 7 in separate worktrees; commit on the phase branch
   without waiting for review (approved for that night); no push, tag or merge; no AI
   attribution in commits. After a usage-limit pause the coordinating session moved the finish
   time and set the cut order (no p = 8/16 series; CI jobs only if time remained).

### Outputs
- `src/core/common/Rng.{hpp,cpp}`; `src/core/perf/`: `BatchGenerator`, `WorkerPool`,
  `SerialRunner`, `ParallelRunner`, `Benchmark`, `BenchmarkReportIO`, `SystemInfo` +
  `SystemInfo_win.cpp` / `SystemInfo_linux.cpp`.
- `src/cli/commands/BenchCommand.*`, `bench` options in `main.cpp`, `all` runs a quick bench.
- `src/app/views/PerformanceView.*`, `ResultStore::benchmark`, registration in `AppShell`.
- Tests: `tests/unit/common/RngTest.cpp`, `tests/unit/perf/{BatchGenerator,WorkerPool}Test.cpp`,
  `tests/numerical/perf/{RunnerAgreement,Benchmark}Test.cpp`.
- `.github/workflows/ci.yml` (`linux-asan`, `linux-tsan`), `tools/bench_report.ps1`,
  `docs/performance.md`, `results/benchmarks/bench_2026-11-01.*` (provisional).

### My edits
- No code edits; review only.

### Verification
- Windows Release `/W4 /WX`: clean. ctest 215/215 (39 new).
- Serial vs parallel: `maxAbsDiff_C` is exactly 0 in tests and in every benchmark run (bitwise
  equal, not just ≤ 1e-8).
- Rng: the C++ standard's reference value (10 000th output of a default-seeded
  `mt19937_64` = 9981545732273789042) is a test.
- The new core/perf sources, CLI and tests were also run through the clang frontend
  (`clang-tidy` from the VS Build Tools, `-Wall -Wextra -Wpedantic -Wshadow -Wconversion
  -Wsign-conversion -Wold-style-cast -Wnull-dereference`): no diagnostics. GCC itself was not
  available, so the Linux CI jobs are untested until the first push; `SystemInfo_linux.cpp` and
  the Phase 3 `Process_posix.cpp` have never been compiled.
- Benchmark runs (report preset, i5-14600K, MSVC Release): under visible load T1 = 1.25 s,
  S(4) = 3.88; later, quieter: T1 = 1.058 s / 1.067 s, S(4) = 4.00 / 4.03.
- Not verified: the Performance view was built but not opened and clicked through in the
  running app (no screenshot yet).

### Incorrect / unsuitable suggestions and how they were caught
| # | Suggestion / output | Problem | How caught | Fix |
|---|---|---|---|---|
| 1 | Spec preset 4 000 × 4 000 for the report "tuned so T1 ≥ 1 s" | Measured T1 = 0.21 s: the design estimate was ~5× off | First `bench --report` run | Preset 10 000 × 8 000 (T1 ≈ 1.06 s, 640 MB per result set), recorded in the code comment, commit and docs |
| 2 | JSON string escaping via a helper that formats a `double` with `%04x` | Passing a double to `%x` is undefined behaviour | Reading the code before the first build | Direct `snprintf` with an `unsigned` argument |
| 3 | Report test that read the CSV with an `std::ifstream` and then deleted the folder | The stream was still open, so `remove_all` failed on Windows ("file in use") | Test failure | Stream closed in its own scope; `remove_all` with `error_code` |
| 4 | Writing several source files through one long shell heredoc | The shell aborted on a quoting error and silently wrote none of the `SystemInfo` files | `ls` of the folder | Files written with the editor tool instead |
| 5 | A `sed` script to cut intermediate `CMakeLists.txt` versions for per-commit builds | Produced a garbled file (a dangling argument line) | Printing the file before using it | Rebuilt from the clean version by hand |
| 6 | Provisional report label "measured while three other builds were running" | Not true for the committed run: the other sessions had stopped by then, so the claim was unverified | Comparing the quieter numbers with the earlier run | Label changed to "not checked to be idle"; both runs cited in `performance.md` |
| 7 | Recorded build flags | `/EHsc` appeared twice (already in `CMAKE_CXX_FLAGS`) | Reading the first bench output | Removed from the extra-flags list |

### Deviations from the spec for me to confirm
- Report preset 10 000 × 8 000 instead of 4 000 × 4 000 (#1); memory for the two result sets
  is ~1.28 GB, under the 1.5 GB target but not by much.
- p = 1 runs the serial runner on the calling thread, not a 1-thread pool.
- `WorkerPool` body returns `Result<void>` (so a case's validation error stops the pool) and
  the pool starts min(p, cases) threads.
- `BenchRow` gained `threads` and `warmupSec`; `BenchmarkReport` gained `orderPreserved`,
  `speedupAssessable` and `resultSetBytes`; `SystemInfo` gained `coreTypes` and `buildType`.
- `generateBatch` returns `Result<std::vector<RodProblem>>` (InvalidInput for bad sizes).
- `bench` also writes three SVG plots; `all` writes its quick bench to the git-ignored
  `results/benchmarks/quick/`.
- No p = 8/16 series (cut for time; discussed, not measured).

### My decisions
- Accepted the 10 000 × 8 000 report preset. 4 000 × 4 000 measured T1 = 0.21 s, too short
  for a stable speedup.
- Cut the p = 8/16 series for time.

---

## 2026-11-02: Embedded integration, Task E (Phase 6)

### Prompts (representative)
1. "Implement Phase 6 (embedded integration, Task E) of UniForge, following
   `docs/design/UniForge_Final_Build_Spec.md` …" The brief listed the scope: design doc +
   hand-written SVG block diagram with **every electrical fact cited** from the official
   datasheet / user manual (or marked as an unverified assumption), the `core/hw` mocks
   (`MockTransport`, state machine, `RetryPolicy`, three adapters, discovery, injectable time),
   the `DeviceManager` thread and the Hardware view, tests, and doc rows.
2. Run in parallel with Phases 4, 5 and 7 on separate branches/worktrees; commit on the branch
   (no push, tag or merge), no AI attribution lines in commits. A second message moved the
   finish time after a usage-limit pause.

### Outputs
- `docs/embedded_design.md`, `docs/embedded_block_diagram.svg`.
- `src/core/hw/`: `DeviceTypes`, `Clock`, `MockTransport`, `DeviceStateMachine`, `RetryPolicy`,
  `IDeviceAdapter`, `MockAdapter`, `Adapters` (MCU/FPGA/JTAG + `discoverDevices`).
- `src/app/services/DeviceManager`, `src/app/views/HardwareView`; AppShell registers the view.
- `tests/unit/hw/HwAdapterTest.cpp`, `tests/negative/hw/HwRecoveryTest.cpp`,
  `tests/unit/services/DeviceManagerTest.cpp`.

### Sources actually read
ST UM1724 Rev 17 and DS10693 Rev 11 (PDF text), the Digilent Basys 3 reference manual (web
page; the site showed a bot check in the browser, which was not bypassed; the text came from a
search service's fetch of the same URL), the SEGGER EDU Mini product page and two SEGGER
knowledge-base pages. The SEGGER UM08001 PDF was downloaded but had no EDU Mini pin data, so it
is not cited. Xilinx UG480 (XADC) could not be fetched; XADC facts are cited from the Basys 3
manual only.

### Verification
- Release build `/W4 /WX` clean; `ctest` 199/199 (23 new: `unit_hw` 10, `negative_hw` 10,
  `DeviceManager` 3). Recovery timings asserted exactly on `ManualClock` (TimeoutAlways =
  4 × 250 ms + 700 ms back-off = 1700 ms simulated).
- Every pin/voltage value in the design doc re-checked against the extracted text (section,
  table and page numbers looked up, not recalled).
- SVG rendered in a browser and the overlapping labels fixed.
- `UniForge.exe` launched and logged ready. **Not verified on screen:** the click-through
  (Discover → Connect → Inject timeout → "recovered after 1 retry"). It is covered by the
  `DeviceManager` tests, but I still need to try it in the app.

### Incorrect / unsuitable suggestions and how they were caught
| # | Suggestion | Why it was wrong | Caught by | Fix |
|---|---|---|---|---|
| 1 | Spec §7.5: J-Link "→ 10/20-pin debug header of a chosen target" | The EDU Mini has a **9-pin 0.05" Samtec FTSH** connector, a 1.8 V / 3.3 V interface only, no target supply, and SEGGER describes it for Cortex-M targets | SEGGER product page + KB | Target = the Nucleo's STM32F446 over CN7; Basys 3 keeps its on-board USB-JTAG |
| 2 | The obvious reading "external probe goes on the Nucleo's SWD header CN4" | CN4 is the ST-LINK's *output* to an external board; an external probe reaches the on-board MCU via CN7-13/15 with both CN2 jumpers OFF | UM1724 §7.4.4–7.4.5, Table 29 | Ownership table in the design doc |
| 3 | First draft of `RetryPolicy` built "retry"+"s" | Would have printed "3 retrys" in the hint | Reading the code before the first build | Explicit "retry"/"retries" |
| 4 | First SVG layout | Wire labels overlapped the legend and the USB line crossed it | Browser render | Legend and USB route moved |
| 5 | Spec's "powered USB hub" | No document read here sets the boards' combined USB draw (Basys 3 USB current is not stated) | Writing the current-limit row | Kept as an unverified assumption |

### Deviations from the spec for me to confirm
- `simulated idcode` uses 0x4BA00477 and is labelled simulated and unverified. I did not check
  it against the Arm/ST reference manuals.
- The view owns the `DeviceManager` (created on first display) instead of `App`, so the shared
  `AppContext` did not change during parallel work. It can move to `App` at integration.
- A device that is timing out blocks the DeviceManager tick for up to ~1.7 s (the other cards
  stop updating during that time; the UI thread is not blocked).
- Disconnect fail mode: the link drops after 3 successful exchanges; its exhausted error is
  reported as `DeviceFault` (the spec names no code).
- The sparkline and retry trail are drawn with plain ImGui (`PlotLines`, text) inside the view
  until Phase 4's shared widgets land.

### My decisions
- The devices are clearly labelled simulated throughout. No claim of hardware testing is made.
- Accepted that `DeviceManager` stays inside the view for now, to avoid shared-context changes
  during parallel work.
- While clicking through the packaged app I found a "conflicting ID" debug popup on the
  Hardware cards. All sparklines used the same ImGui ID. I had it fixed before the merge (see
  the integration entry).

---

## 2026-11-02: Backpropagation, Task F (Phase 7)
Tool: Claude Code (Claude Opus 5.5), in its own git worktree on branch `phase7-backprop`, in
parallel with Phases 4–6. The handwritten derivation (F1) is **not** AI work: I write it myself;
the AI only wrote the checklist of what it must contain.

### Prompts (representative)
- "Implement Phase 7 (backpropagation, Task F) following the build spec: `core/ml` forward,
  backward, simultaneous update, central differences, train; tests to 1e-6 against the brief;
  CLI `backprop` with golden files; Backprop view; `docs/backprop.md` explaining the
  **measured** loss curve; the handwritten-appendix checklist. Check whether the loss is really
  non-increasing rather than forcing the test."

### Outputs (files, functions)
- `src/core/ml/{TinyNet,GradientCheck,Trainer}.{hpp,cpp}`: `forward`, `backward`, `sgdStep`,
  `centralDiff`, `train`, `BackpropProblem::validate`.
- `src/cli/commands/BackpropCommand.{hpp,cpp}`, `main.cpp` (`backprop`, added to `all`).
- `src/app/views/BackpropView.{hpp,cpp}`, registered in `AppShell.cpp`.
- `src/core/common/SvgPlot.cpp`: thinned log ticks over wide ranges.
- Tests: `tests/numerical/ml/BackpropTest.cpp`, `tests/negative/ml/BackpropInputTest.cpp`,
  `SvgPlot.WideLogAxesThinTicksToPowersOfTen`, `tests/integration/backprop_golden.cmake` +
  `tests/golden/backprop/*.csv`.
- Evidence: `results/csv/{backprop_gradients,backprop_update,loss_curve}.csv` (+ sidecars),
  `results/plots/loss_curve{,_logy}.svg`. Docs: `docs/backprop.md`,
  `docs/appendix_handwritten/{CHECKLIST,README}.md`.

### My edits
- No code edits; review only. The derivation in the appendix is written by hand.

### Verification (tests run, numbers checked, manual checks)
- Release build (`/W4 /WX`) clean; `ctest` 193/193 (17 new).
- Every brief value reproduced to 1e-6 (forward, four gradients, update; max deviation of the
  update from the brief's 6-decimal values 4.5e-7).
- FD check: max |analytic − numeric| = 1.13e-10, as the error budget predicts (round-off
  ~1e-16·J/ε). The check was also shown to fail on NaN input and with a zero tolerance, and to
  pass at a generic point (x = −1.3, v = 2.5), where a missing factor x or v would show.
- Loss monotonicity was **measured before writing the test**: J decreases strictly at every
  step (1.182542 → 1.70e-20), so the test asserts strict decrease and J₁₀₀ < 1e-6·J₀.
- The explanation in `docs/backprop.md` was cross-checked against a formula: the linearised rate
  (1 − lr·‖∂ŷ/∂p‖²)² at the final parameters gives 0.634607 vs the measured 0.634606.
- Existing FEA plots regenerated byte-identical after the `SvgPlot` change.
- Not verified: the Backprop view has **not been looked at on screen** yet (it builds; the
  screenshot step was skipped). The golden CSVs depend on `std::tanh`; not yet run on Linux,
  where a different libm could change the last of the 17 digits.

### Incorrect / unsuitable suggestions and how they were caught
| # | What the AI produced | Problem | How it was caught | Resolution |
|---|---|---|---|---|
| 1 | A `perl` one-liner inserting a C++ function containing `"\n"` string escapes | Perl expanded the escapes into real newlines inside string literals; the CLI did not compile (C2001) | Build output | Function rewritten with an exact-text edit |
| 2 | First log-y loss plot used the existing 1-2-5 tick rule | Over 20 decades it printed 64 overlapping tick labels | Reading the SVG's text labels | Powers of ten only beyond 4 decades, thinned to ≤ ~10; regression test added |
| 3 | View text with `↺` (reset) and `←` (assignment arrow) | Neither glyph is in `GlyphRanges.hpp`, so both would render as `?` | Checking the glyph table before running | Replaced with plain words |
| 4 | Plot axis limits pinned with `ImPlotCond_Always` | Would have stopped the user zooming the plot | Review before building | `ImPlotAxisFlags_AutoFit` instead |

### Deviations from the spec for me to confirm
- `forward` takes `y` as well as `x` (the spec lists `forward(Params, x)`), because `Forward`
  holds `err` and `loss`.
- `GradCheck` gained `passEach` (per parameter), `eps` and `tolerance`; `train` has an optional
  `finalParams` output.
- Two SVGs (`loss_curve.svg` linear, `loss_curve_logy.svg`) instead of one plot with an inset.
- Also wrote `backprop_update.csv` (not in spec §7.6, asked for in the phase brief).
- The view recomputes on the UI thread on every input change instead of using JobRunner
  (microseconds of work); the badge is a local helper until the Phase 4 VerificationBadge
  lands.
- `SvgPlot` (Phase 2 code) changed for wide log axes; ≤ 4-decade plots unchanged.

### My decisions
- The derivation in the appendix is mine and is written by hand. The AI only provided the
  checklist of what it must contain.
- Accepted the two-SVG loss plot and the extra `backprop_update.csv`.

---

## 2026-11-02: Integration and review (Phases 4–7)
Tool: Claude Code (Claude Opus 5.5).

### Why the work ran in parallel
With the deadline (2026-11-02, 10:00) fixed, building Phases 4–7 one after another would not
fit. I re-planned so that the four phases ran at the same time, each in its own session and
git worktree. Nothing reached `main` until I had reviewed it.

### Prompts (representative)
- "Adjust the plan" (then: four sessions, one per phase).
- "Fix these two, then fast-forward main, tag Phases 4–7 and push" (with two screenshots of
  defects I found in the packaged app).

### Outputs
- An `integration` branch merging the four phase branches. All views are registered in
  `AppShell`, and the `ResultStore` slots, CLI usage and `all` are merged.
- A broken merge of `tests/CMakeLists.txt` was fixed: a closing `LIBS uniforge_core)` line had
  been dropped.

### Verification
- Release build clean under `/W4 /WX`. 289/289 tests pass (unit 145, negative 69, numerical
  61, integration 14).
- `tools/package.ps1` passes the dependency and hygiene checks. Launch-to-ready is 0.128 s.
- I clicked through every view of the packaged app myself.

### Incorrect / unsuitable output and how it was caught
- **Hardware view:** every telemetry sparkline used the label `##spark` in the same ID scope.
  Dear ImGui showed its "2 visible items with conflicting ID" error over the device card.
  Neither the unit tests nor the Phase 6 session caught it, because it shows only when a device
  is connected and drawing. I found it on screen. Fix: `PushID` per channel.
- **Thermal view:** after a study, a plain blue bar sat next to "Run study". The `RunBar`
  progress bar had a fixed 220 px width, wider than the parameters panel, so the "100 %" label
  was clipped. I found it on screen. Fix: the bar shrinks to the free width and wraps to its
  own line when there is no room.

- **Hardware view (Disconnect):** the button flickered and ignored my first click. The device
  thread set `busy` during every 10 Hz telemetry tick, which disabled the action buttons about
  10 times a second. I found it on screen. Fix: only user commands set `busy`.

### My decisions
- Found the three defects above by testing the package myself, and held the push until they
  were fixed.
- Set the Candidate ID (`RA2311042010060`) used for the package name, and the licence holder
  name.
- Kept the CHANGELOG newest-first and this log in date order.

---

## Entry template

```markdown
## YYYY-MM-DD: <phase / topic>
### Prompts (representative)
### Outputs (files, functions)
### My edits
### Verification (tests run, numbers checked, manual checks)
### Incorrect / unsuitable suggestions and how they were caught
```
