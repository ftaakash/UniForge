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

## 2026-11-02: Hardening, trimmed (Phase 8)
Tool: Claude Code (Claude Opus 5.5).

### Prompts (representative)
- "Works, start Phase 8/9" (after confirming the Disconnect fix in the packaged app).

### Outputs
- `src/app/platform/CrashHandler.{hpp,cpp}`, the `--simulate-crash` option and its README
  row.
- `tools/edge_case_runs.ps1` and its output `results/edge_cases.md`.

### Verification
- The package passes its dependency check with an unchanged import list: the crash handler
  uses only `KERNEL32` and `USER32`. 289/289 tests pass.
- `edge_case_runs.ps1`: 5/5 pass (see `results/edge_cases.md`).

### Incorrect / unsuitable suggestions and how they were caught
- The first read-only test denied `W,D,DC` with inheritance on the package copy. Windows then
  refused to start `Launch.exe` at all ("Access is denied"), so the test did not show the
  fallback. Now it denies only creating files and folders in the package folder, which is what
  a read-only share or media looks like to the app.

### My decisions
- Phase 8 is trimmed for the deadline: crash handler and edge-case runs are in; the 200 % DPI,
  keyboard and high-contrast pass is cut and listed as a limitation.
- Crash report as text instead of a minidump, so the package keeps its OS-only DLL list.

---

## 2026-11-02: Delivery (Phase 9)
Tool: Claude Code (Claude Opus 5.5).

### Prompts (representative)
- "Works, start Phase 8/9."

### Outputs
- `docs/report/report.html` → `report.pdf`, `docs/ownership.md`, `docs/walkthrough.md`,
  `docs/demo_script.md`, the README rewrite, `tools/build_report.ps1`, the git bundle step in
  `tools/package.ps1`.
- Screenshots in `docs/demo/screenshots/`: the AI drove the packaged app through each view
  (load samples, run the study, run the report benchmark, connect devices and inject a timeout)
  and captured the window.

### Verification
- Every number in the report was checked against the committed evidence files
  (`results/csv/convergence.csv`, `results/benchmarks/*.md`, `backprop_gradients.csv`,
  `loss_curve.csv`). The page count is checked by `build_report.ps1` (5 of at most 6 pages).
- The PDF contains no local paths (checked with grep).

### Incorrect / unsuitable suggestions and how they were caught
- The first report draft printed at 8 pages, over the 6-page limit, because of full-width
  screenshots and forced page breaks. Rebuilt with paired figures: 5 pages.
- The first draft said "no exceptions cross module boundaries". This was removed because it
  was not checked against the code.
- While taking the screenshots, the Overview still showed rows D–F as "Pending · Phase 5/6/7".
  Phase 4 wrote that before those views existed. It now shows live status (Phase 8 commit).
- A `sed` replacement containing `\U` upper-cased part of a comment in `package.ps1` (GNU sed
  case conversion). It was caught on review and fixed before commit.

### My decisions
- Kept the report to the brief's sections, and referenced the detailed notes rather than
  repeating them.
- Screenshots use the light theme, as the spec says, and show real runs, not mock-ups.
- I record the demo and fill in the effort hours myself.

---

## 2026-11-15: V2 analysis, design review, system design and build spec (Phase V2-0)

The company sent an Assignment Extension Notice asking for a V2: three different solver types on
a thermal analysis of a more complex 3D object, delivered as one PDF report without source code.

### Prompts (representative)
1. Plan V2 from the notice and the v1 repository; write an analysis and plan; do not build anything.
2. `/design:design-system`: "analyse the V2 plan, suggest amendments if necessary, evaluate its
   effectiveness on cost and robustness: time, memory, iteration count, and how each one copes
   with mesh size and with hard-to-solve (badly conditioned) setups."
3. "Fold them in" (apply the review's amendments to the plan).
4. `/engineering:system-design` on the revised plan: module boundaries, interfaces, data models,
   harness and report pipeline, with an alternative design to compare.
5. "Merge into build spec", then "Start V2-0".

### Outputs (files)
- `docs/design/v2/UniForge_V2_Analysis_and_Plan.md` (revision 2, amendments A1–A15 marked inline)
- `docs/design/v2/UniForge_V2_Design_Review.md` (16 issues, amendments A1–A15, figure tokens)
- `docs/design/v2/UniForge_V2_System_Design.md` (Design A vs Design B)
- `docs/design/v2/UniForge_V2_Build_Spec.md` (merged specification the V2 build follows)
- `docs/report_v2/figure_tokens.md` (shared figure style, amendment A13)

### My edits
- Chose to fold in all review amendments, to run the system-design pass, and to merge the three
  documents into one build spec. The choices among the adopted defaults (spec §0.2) and the
  deadline are still open.

### Verification (tests run, numbers checked, manual checks)
- No code yet, so nothing was run. Every number in the V2 documents is either copied from v1
  files (e.g. 13.7 h actual vs 46 h planned, heat-sink volume 2.416e-5 m³, peak 188.2 °C) or
  tagged [EST] (estimate, to be measured). Every unchecked fact or citation is tagged [VERIFY].
  Neither tag may reach the PDF.
- Checked mechanically that the merged spec contains every requirement, amendment, hypothesis,
  experiment and figure ID from the three source documents, and no e-mail address.

### Incorrect / unsuitable suggestions and how they were caught
The review pass (`/design:design-system`) found 16 issues in the AI-written plan. The five rated high:

| # | Unsuitable suggestion | Why it was wrong | Resolution |
|---|---|---|---|
| I1 | Refine the mesh by integer factors r = 1, 2, 3 | DOF grows ×8, ×27, so the direct solver would get only two data points before its memory wall; no slope or crossover could be fitted | Geometric ladder s_k = s₀·2^(−k/3), DOF doubles per step (A1) |
| I2 | Stop iterative solvers on a relative residual of 1e-10 | The convection-only heat sink is near-singular; a small residual does not bound the error, and the mean temperature converges last | Time-to-accuracy against a reference, energy imbalance and condition estimate per solve (A2, A3) |
| I3 | Assemble through COO triplets | ~3 GB plus a sort buffer at 3 × 10⁶ cells [EST]: assembly, not the solvers, would set peak memory | Assemble straight into CSR from the grid's known pattern (A4) |
| I5 | No machine-time budget | The experiment campaign would probably take longer than the coding | Caps, repetition policy, dry run, one overnight batch (A6) |
| I11 | Verify the 3D FEA before any sparse solver exists | Verification would be limited to meshes the dense oracle can handle | Build Jacobi-PCG first (A10) |

Also: the plan's hypothesis that "on the cube all solvers look alike" is probably false for the
slender v1 bar (quasi-1D, where the direct solver should win clearly); E0 now compares the bar with
a true cube (A11). The system-design pass found that v1's `perf::WorkerPool` starts and joins new
threads on every `run()` call, which suits the v1 batch but would distort solver timings; V2 uses
a persistent kernel pool instead.

---

## 2026-11-18: Sparse kernels (Phase V2-1)

### Prompts (representative)
1. (Relayed from the planning session) "continue building v2": start Phase V2-1 without waiting
   for the deadline, take the recommended defaults for the open decisions and record them in the
   plan, one commit per phase on branch `v2`, stop for review at the end of each phase.

### Outputs (files, functions)
- `src/core/sparse/CsrMatrix.*`: `CsrPattern`, `CsrMatrix` (32-bit columns, 64-bit row
  offsets), `fromTriplets`, `buildPattern`, `validate`, `isSymmetric`, `toDense`.
- `src/core/sparse/KernelPool.*`: persistent thread pool; fixed 4096-row chunks independent of
  the thread count; per-chunk partial results combined in chunk order.
- `src/core/sparse/VectorOps.*`: `dot`, `sum`, `norm2`, `normInf`, `maxAbsDiff`, `axpy`,
  `xpay`, `multiplyPointwise`, `spmv`, `residual`, `diagonal`.
- `src/core/sparse/SparseOps.*`: `transpose` (counting sort), `multiply` (Gustavson SpGEMM with a
  symbolic pass for exact allocation).
- `tests/unit/sparse/*`: 29 tests (CTest target `unit_sparse`, label `unit`).
- Spec §0.2a and plan §9: the adopted defaults and the one-commit-per-phase rule.

### My edits
- None to the code yet; this phase is for your review.

### Verification (tests run, numbers checked, manual checks)
- Windows MSVC 19.44, warnings as errors: Release and Debug builds clean; **318/318 tests pass in
  both** (289 from v1 + 29 new).
- Determinism: reductions and every kernel give bit-identical results for 1, 2, 3, 7 and 20
  threads. The test data mixes magnitudes 1e-8 … 1e8 and the test first asserts that a plain
  left-to-right sum differs from the chunked sum, so the check cannot pass by luck.
- SpMV, residual, transpose and SpGEMM agree with dense references; a Galerkin product PᵀKP of a
  symmetric K is symmetric; SpGEMM allocates exactly nnz(C).
- 20 000 back-to-back pool calls without deadlock; an exception in one chunk is rethrown after
  the other chunks finish and the pool stays usable.
- clang-format 19 check passes on the new files.
- **Not run:** the Linux GCC 13 ASan/TSan jobs (they run in CI after a push). GCC's stricter
  warnings (-Wconversion, -Wsign-conversion, -Wshadow) were checked by reading, not by compiling.
- Not measured yet: the pool's per-call overhead (planned for V2-8 with the other timing checks).

### Incorrect / unsuitable suggestions and how they were caught
- The first draft of `KernelPool::reduceMax` combined partial maxima with `std::max`, which
  silently drops a NaN (`max(x, NaN)` returns `x`). A NaN temperature would then hide behind a
  finite maximum. Caught while writing the kernels; `reduceMax` and `normInf` now propagate NaN,
  and `VectorOps.NormInfPropagatesNaN` tests it.
- The first draft of the Galerkin test called a dense-multiply helper with a vector where it
  takes a matrix (it would not have compiled); caught on review before the first build and
  replaced by two separate dense checks (K·P, then Pᵀ(K·P)).
- The build logs of the first build were written into the worktree root, where `.gitignore`
  does not cover `*.log`; deleted before staging, later logs go to `build/`.

---

## 2026-11-18: Geometry and mesher (Phase V2-2)

### Prompts (representative)
1. "Start V2-2" (after approving and pushing V2-1, whose CI run passed on Windows and Linux
   ASan/TSan).

### Outputs (files, functions)
- `src/core/fem3d/Design.*`: `Box`, `Material`, `FaceBc`, `Design`, `validate`; factories
  `makeV1Bar` (L0a), `makeCube` (L0b), `makePlate` (L1), `makeFinnedPlate` (L2), `makeHS7` (L3),
  `makePinFin` (L4) with `HeatSinkParams` / `PinFinParams`.
- `src/core/fem3d/HexMesh.*`: `buildAxis` (mandatory lines, fixed layers, graded gaps),
  `buildMesh` (cell ownership, compact node numbering, boundary faces with box/side, statistics),
  `extractSurface` (outward triangles), cell/face helpers.
- `tests/unit/fem3d/*`: 19 tests (CTest target `unit_fem3d`).
- Spec §3.1 (ladder clarified: the die is the heat source at L1–L3, the TIM only at L3) and
  §3.4 (measured mesh sizes replace the estimate).

### My edits
- None to the code yet; this phase is for your review.

### Verification (tests run, numbers checked, manual checks)
- Windows MSVC, warnings as errors: Release and Debug clean, **337/337 tests pass in both**.
- Hand-checked numbers used by the tests: v1 bar at s = 10 mm has 100 cells, 404 nodes and
  402 boundary faces; HS-7 aluminium volume 2.416e-5 m³ (equal to v1's heat-sink STL); HS-7
  convective area 1840 + 800 + 7 × 2264 = 18 488 mm², reproduced by the mesh at two spacings.
- Mesh volume equals the design volume to 1e-10 relative for all six ladder objects; the
  surface is watertight (every boundary edge in exactly two faces) with outward normals; the
  divergence-theorem volume of the exported surface equals the design volume; the STL parses
  with the v1 importer (no degenerate facets, expected bounding box).
- Grading: no cell larger than s and no neighbour ratio above 1.5 on every HS-7 axis, with 1
  and 2 TIM cells, at two spacings.
- Measured mesh sizes along the ladder s = 1.0 … 0.198 mm: 56 015 … 3 759 720 nodes, meshing in
  ≤ 0.3 s (recorded in spec §3.4; measured with a temporary test that was then removed).
- **Not run:** Linux CI (after the push).

### Incorrect / unsuitable suggestions and how they were caught
- The spec's DOF table (an AI estimate, tagged [EST]) was 16–60 % low: it ignored the extra
  cells that grading next to the 0.1 mm TIM and the fins adds. Replaced by measured values.
- The spec described ladder level L2 as "single material", which contradicts the die being the
  heat source; clarified in the spec.
- First draft of the axis builder found the cell count of a gap by increasing n one at a time,
  each step O(n): an absurd spacing (1 µm on the 1 m bar) would have hung for hours before the
  size limit was checked. Caught on review: the count is now found by doubling and bisection,
  and `buildMesh` refuses an oversized grid before generating any line
  (`HexMesh.RefusesAbsurdSpacings`).
- First draft of the design test indexed the HS-7 boxes as if the fins came right after the base;
  the factory adds TIM and die first. Caught on review; the test now asserts the box name.
- Two first-run test failures were tolerances tighter than floating-point rounding (cube volume
  off by 1.2e-12 relative, a grade ratio of 1 + 7e-14 on equal cells); loosened to 1e-10 and
  1e-9, not a mesher change.

---

## 2026-11-21: Solver interface and Jacobi-PCG (Phase V2-3)

### Prompts (representative)
1. "Start V2-3" (after approving and pushing V2-2, whose CI run passed on Windows and Linux).

### Outputs (files, functions)
- `src/core/solvers/SolverTypes.hpp`: `SolverKind` and `SolveStatus` with their labels,
  `MemoryFootprint`, `CoordinateHint`, `SolverOptions`, `SolveControl`, `SetupReport`,
  `SolveReport` (status, iterations, solver time with observer time excluded, SpMV count, bytes
  moved and SpMV-equivalents, residual history, per-iteration timestamps, κ estimate, true
  residual, masked residual sum).
- `src/core/solvers/LinearSolver.hpp` + `SolverFactory.cpp`: the two-phase interface
  (`setup` once, `solve` per load case) and `makeSolver` (PCG-J now; other kinds return an
  error naming the phase that adds them).
- `src/core/solvers/Observers.*`: `IterationObserver`, `ErrorVsReference`, `Snapshot`.
- `src/core/solvers/krylov/Lanczos.*`: Sturm-bisection extreme eigenvalues of a symmetric
  tridiagonal matrix; κ estimate from the CG coefficients.
- `src/core/solvers/krylov/Preconditioner.*`, `Pcg.*`: preconditioner interface, identity
  (plain CG, tests only) and Jacobi; PCG with Breakdown, MaxIterations, Stagnated, TimeCap,
  Cancelled and OverBudget statuses.
- `tests/unit/solvers/*`: 15 tests; `tests/lint/module_rules.cmake`: CTest check of the module
  dependency rules (spec §4.2).

### My edits
- None to the code yet; this phase is for your review.

### Verification (tests run, numbers checked, manual checks)
- Windows MSVC, warnings as errors: Release and Debug clean, **353/353 tests pass in both**.
- PCG-J reproduces the dense solution of a 400-unknown 2D Laplacian to 1e-9 (relative); the
  true residual after the solve is below 1e-11.
- κ estimate on the 1D Laplacian (n = 200), where the closed form is known: between 0.9× and
  1× of the exact value, as expected for a Lanczos lower bound.
- Bit-identical iterates, iteration counts, residual histories and κ estimates for 1, 2, 3 and
  8 threads (8100 unknowns, two kernel chunks).
- Jacobi takes fewer than a third of plain CG's iterations on a badly scaled Laplacian
  (scaling 1e-3 … 1e3).
- Every failure status is produced by a constructed case (iteration cap, time cap 0, cancel,
  stagnation window 2, 1-byte memory budget, indefinite matrix); misuse returns errors.
- Module-rule lint: a deliberately added `#include "core/fem3d/..."` under `solvers/` made the
  check fail (exit 1); removed, it passes.
- **Not run:** Linux CI (after the push). Not measured: observer overhead and kernel-pool
  overhead (V2-8).

### Incorrect / unsuitable suggestions and how they were caught
- The first PCG test file named its helper struct `Run`. Inside a googletest `TEST` body that
  name resolves to the fixture's inherited `testing::Test::Run()` member, so the build failed
  with "undeclared identifier"; renamed to `Outcome` / `runSolver`.
- The first observer test solved a 900-unknown system with the O(n³) dense oracle (~7e8
  operations), which would be slow in Debug; reduced to 400 unknowns before the first build.

---

## 2026-11-21: 3D finite elements and verification (Phase V2-4)

### Prompts (representative)
1. "update the repo" (clarified by a question: update the README/CHANGELOG on `v2`), then
   "Push it" and "Start V2-4".

### Outputs (files, functions)
- `src/core/fem3d/Element.*`: Q1 hexahedron stiffness and loads as tensor products of the 1D
  linear-element matrices; bilinear face convection matrix and loads; Gauss quadrature for
  non-uniform source and ambient fields (used by the manufactured solution).
- `src/core/fem3d/Assembly.*`: `LinearSystem`, `assemble` (pattern from grid connectivity, CSR
  scatter, convection, symmetric Dirichlet elimination with saved rows for reactions, cost).
- `src/core/fem3d/Post.*`: `energyAudit`, `reactions`, `peakTemperature`, `sampleTemperature`,
  `errorNorms`.
- Tests: `tests/unit/fem3d/ElementTest.cpp`, `AssemblyTest.cpp` (12), and
  `tests/numerical/fem3d/VerificationTest.cpp` (6, label `numerical`).
- Spec §3.5: measured assembly cost; README and CHANGELOG updated for V2-4.

### My edits
- None to the code yet; this phase is for your review.

### Verification (tests run, numbers checked, manual checks)
- Windows MSVC, warnings as errors: Release and Debug clean, **371/371 tests pass in both**.
- Hand-checked element values: unit-conductivity cube stiffness diagonal h/3, edge 0, face and
  body diagonals −h/12; ∫x·N over the unit cube 1/12 and 1/24; face mass h·A/9 and h·A/36.
- Verification results (all from the test output, PCG-J at relative residual 1e-13):

  | Check | Result |
  |---|---|
  | Patch test, harmonic trilinear field, Dirichlet / convection faces | nodal error < 1e-9 / < 1e-8 |
  | v1 bar in 3D (s = 10 and 5 mm) | max nodal error 1.1e-11 / 6.6e-11 K; ends 0.4200000000 / 0.5800000000 W |
  | Manufactured solution, 4 → 32 cells | RMS error 0.156 → 0.00248 K; observed order 1.999 (L2), 2.001 (nodal) |
  | Single fin vs 1D fin theory | heat 3.01756 vs 3.01770 W (−0.005 %); tip 77.504 vs 77.481 °C |
  | HS-7, s = 2 mm (13 200 DOF) | relative energy imbalance 1.9e-12; T_max 99.03 °C in the die; PCG-J 768 iterations, κ_est 1.37e5 |

  (HS-7's T_max follows from the chosen inputs P = 30 W, h = 25 W/(m²·K); it is not a
  validated prediction.)
- Fault injection, as in v1 (deliberate, code restored afterwards): removing the column move
  `f_j −= K_jd·T_d` from the Dirichlet elimination failed the patch, bar and fin tests; the bar's
  energy audit showed an imbalance of 104–109 % and the ends carried −9.5 / −1.5 W. The two
  convection-only tests (manufactured solution, HS-7) correctly still passed.
- Assembly cost on HS-7 measured with a temporary test (removed): 4.67 s and 1.16 GB for
  3 759 720 DOF, ~25.7 entries per row.
- **Not run:** Linux CI (after the push).

### Incorrect / unsuitable suggestions and how they were caught
- The first README test filter used `Element` as a pattern, which would also have matched v1's
  `ElementMatrices.*` tests; changed to `Element\.` and checked that the filter selects exactly
  the 82 V2 tests.
- The first fin tolerances (1 % heat, 0.2 K tip) were much looser than the measured agreement;
  tightened to 0.2 % and 0.1 K so the test can catch a real regression.

---

## 2026-11-21: Sparse direct Cholesky (Phase V2-5) and the main README note

### Prompts (representative)
1. "Start V2-5".
2. "Approve and push, also the update the status of v2 in the readme of the main branch"
   (sent while V2-5 was still being measured; V2-5 was finished, fully tested, then pushed).

### Outputs (files, functions)
- `src/core/solvers/direct/Ordering.*`: `naturalOrdering`, `rcmOrdering`,
  `nestedDissectionOrdering`, `makeOrdering`, `isValid`.
- `src/core/solvers/direct/Cholesky.*`: `DirectCholesky` (ordering, symbolic with exact column
  counts and budget check, up-looking numeric factorisation, triangular solves, factor access).
- `SolverTypes.hpp`: setup report gains timed phases, predicted/actual nnz(L) and flops;
  options gain the dissection leaf size. `makeSolver` builds DIRECT-ND and DIRECT-RCM.
- Tests: `tests/unit/solvers/DirectTest.cpp` (10), one verification test (direct vs PCG-J on
  the FE systems); 3D Laplacian and shuffle helpers.
- Spec §5.2: preliminary measured fill and setup times; README and CHANGELOG on `v2`.
- On `main`: a V2 status section in the README (commit `885d091`); v1 content unchanged.

### My edits
- None to the code yet; this phase is for your review.

### Verification (tests run, numbers checked, manual checks)
- Windows MSVC, warnings as errors: Release and Debug clean, **382/382 tests pass in both**.
- L·Lᵀ equals P·K·Pᵀ to 1e-12 for all three orderings; the symbolic prediction of nnz(L) equals
  the actual count in every test and in every measured HS-7 run.
- Same solution as the dense oracle (1e-12 relative) for every ordering; on a 16³ grid nnz(L)
  natural 990 991, RCM 596 204, ND 316 268.
- On the FE systems both orderings match PCG-J to 3.8e-10 K (v1 bar, HS-7 at 2 mm) with
  relative energy imbalance ≤ 3.5e-12.
- Preliminary HS-7 ladder (single run, not the official protocol): table in spec §5.2. RCM is
  refused as over budget at 568 152 DOF (≥ 707 M entries predicted, after 2.4 s); ND factors it
  in 327.5 s.
- **Not run:** Linux CI (after the push).

### Incorrect / unsuitable suggestions and how they were caught
- My first threshold for the direct solve's true relative residual (1e-12) failed on HS-7:
  measured 9.5e-12. Cholesky is backward stable, but the convection-only system is badly
  conditioned (κ_est ~1e5), so this is the expected size, not a bug; threshold set to 1e-10
  with the reason in a comment. This is the conditioning effect on direct accuracy predicted by
  hypothesis H4.
- A Python edit inserted `\n` into a C++ string literal as a real line break; the build failed
  and the literal was fixed by hand.
- Ordering expectation not borne out at small sizes: I expected nested dissection to be faster
  as well as smaller; measured, its numeric phase is slower up to 170 k DOF (fewer flop/s on its
  more scattered columns). Recorded as measured, not as the expectation.

---

## 2026-11-22: IC(0) preconditioner (Phase V2-6) and the V2-5 Linux CI fix

### Prompts (representative)
1. "Start V2-6".

### Outputs (files, functions)
- `src/core/solvers/krylov/Ic0.*`: `Ic0Preconditioner` (zero-fill incomplete Cholesky, shift
  ladder, triangular-solve apply, factor access).
- `SolverTypes.hpp`: setup report gains the shift used and the retries; `Pcg::setup` passes the
  preconditioner's timing, detail, shift and retries on; `makeSolver` builds PCG-IC0.
- Tests: `tests/unit/solvers/Ic0Test.cpp` (5), an element test on positive edge couplings, and a
  verification test (PCG-IC0 vs PCG-J on the bar and HS-7).
- Commit `c0b2565`: fix for the failed Linux build of `v2-phase-5`.
- Spec §5.3, H8 and K3 corrected/updated; README and CHANGELOG.

### My edits
- None to the code yet; this phase is for your review.

### Verification (tests run, numbers checked, manual checks)
- Windows MSVC, warnings as errors: Release and Debug clean, **389/389 tests pass in both**.
- IC(0) equals the exact Cholesky factor on a tridiagonal matrix (PCG converges in 1 iteration).
- Iterations to 1e-10 / 1e-13: 2D Laplacian 1600 unknowns Jacobi 143 vs IC(0) 49; 3D 4096
  unknowns 74 vs 28; v1 bar 200 vs 87; HS-7 at 2 mm 800 vs 117 (κ_est 1.37e5 vs 2.0e3). Same
  temperatures as PCG-J to 1.4e-12 K on HS-7.
- Kershaw's 4 × 4 SPD matrix (checked SPD by the exact factorisation): unshifted IC(0) fails at
  row 3; the ladder recovers with α = 1 after four failed shifts; a ladder {0, 1e-6} ends in
  Breakdown.
- **Not run:** Linux CI for this phase and for the V2-5 fix (both after the push).

### Incorrect / unsuitable suggestions and how they were caught
- **V2-5 broke the Linux build** (CI of tag `v2-phase-5`): GCC 13 with ASan/UBSan reported
  `-Warray-bounds`, an error under `-Werror`, on `rep.residualHistory = {value};`. Windows was
  unaffected and I had pushed after a Windows-only check. Fixed with `assign()` in commit
  `c0b2565`; the published tag `v2-phase-5` was left as it is (not rewritten). Lesson: GCC-only
  diagnostics are checked only by CI, so the Linux job result has to be confirmed before a phase
  counts as green.
- **The design documents' claim "Q1 hexes with aspect ratio > √2 give positive off-diagonals"
  (an AI statement tagged [VERIFY]) is wrong for 3D.** √2 is the 2D bilinear threshold; for a
  trilinear box the edge coupling is −dy·dz/(9dx) + dx·dz/(18dy) + dx·dy/(18dz), positive as soon
  as the edge is the longest. Caught while writing the IC(0) tests; corrected in the spec and
  pinned by a test.
- **Hypothesis H8 not confirmed at the tested size:** IC(0) did not break down on HS-7 at 2 mm
  despite 68 872 positive off-diagonals. Recorded as measured; the full E3 sweep decides.

---

## 2026-11-22: Smoothed-aggregation AMG (Phase V2-7)

### Prompts (representative)
1. "Start V2-7".

### Outputs (files, functions)
- `src/core/solvers/amg/Amg.*`: `strongConnections`, `aggregate`, `tentativeProlongator`,
  `smoothedProlongator`, `AmgHierarchy` (build, symmetric V-cycle, statistics, footprint),
  `AmgPreconditioner` (AMG-PCG), `AmgSolver` (stand-alone AMG).
- `SolverTypes.hpp`: AMG options (threshold, per-level decay, coarse size, levels, sweeps) and
  hierarchy statistics in the setup report; `Pcg::setup` keeps the preconditioner's full report;
  `makeSolver` builds all six variants.
- Tests: `tests/unit/solvers/AmgTest.cpp` (7), a verification test (AMG and AMG-PCG vs PCG-J on
  the bar and HS-7); the factory test now expects every kind.
- Spec §5.4 (measured) and K2; README and CHANGELOG.

### My edits
- None to the code yet; this phase is for your review.

### Verification (tests run, numbers checked, manual checks)
- Windows MSVC, warnings as errors: Release and Debug clean, **397/397 tests pass in both**.
- V2-6's CI run passed on all jobs, so the V2-5 Linux fix (`c0b2565`) is confirmed.
- P·1 = 1 on a zero-row-sum matrix; coarse operators symmetric and factorisable; the V-cycle
  preconditioner satisfies yᵀM⁻¹x = xᵀM⁻¹y and xᵀM⁻¹x > 0 on random vectors.
- 3D Laplacian, stand-alone AMG to 1e-8: 15 / 16 / 16 / 19 cycles for 10³ / 16³ / 24³ / 32³
  (mean factor 0.28–0.36). PCG on 24³ to 1e-10: Jacobi 109, IC(0) 39, AMG 11 iterations.
- HS-7 at 2 mm: AMG-PCG 84 iterations to 1e-13 (PCG-IC0 117, PCG-J 800); stand-alone AMG
  673 cycles to 1e-11; temperatures equal to PCG-J's within 3.4e-11 K, energy balanced to 2e-12.
- **Not run:** Linux CI for this phase (after the push).

### Incorrect / unsuitable suggestions and how they were caught
- **Constant strength threshold.** My first version used θ = 0.08 on every level. The
  mesh-independence test failed: cycles grew 16 → 30 → 56 → 89 from 10³ to 32³. A temporary
  diagnostic showed the second-level aggregation discarding most coarse couplings; halving θ per
  level (the schedule usually described for smoothed aggregation, [VERIFY] against Vaněk et al.)
  restored 15–19 cycles. The decay is now an option with default 0.5.
- **Isolated nodes pulled into aggregates.** The first aggregation added every strong neighbour
  of a root, including isolated nodes (identity rows such as Dirichlet nodes); caught by the
  aggregation test and fixed.
- **Rounding asymmetry of the Galerkin product.** Pᵀ(A·P) failed an exact symmetry check on
  entries that cancel; coarse operators are now averaged with their transpose, which keeps the
  V-cycle exactly symmetric for CG.
- **Divergence rule too eager.** "Residual grew three cycles in a row" fired on rounding noise
  at ~3e-12 on HS-7; now it also requires the residual to be 10× above its best.
- **The plan's expectation (H4) that AMG is robust to the TIM did not hold for stand-alone AMG:**
  673 cycles on HS-7 against 56–62 on the same mesh without the TIM, not cured by θ or a second
  sweep. Recorded as a finding (risk K2) instead of being tuned away; AMG-PCG remains the
  fastest-converging Krylov variant.

---

## 2026-11-22: Experiment harness and dry run (Phase V2-8, part 1)

### Prompts (representative)
1. "Start V2-8".

### Outputs (files, functions)
- `src/core/study/Plan.*`: `RunSpec`, `planExperiments` (E0–E8 and the CI smoke plan T0; 241
  runs, dry run 85), `findRun`, `makeDesign`, `systemKey`, `ladderSpacing`.
- `src/core/study/Runner.*`: `RunRecord` (CSV row with a fixed column list), `executeRun`
  (assembly, references, repetitions by run time, observer, history, energy audit,
  verification checks of E1), reference files.
- `src/core/study/Collect.*`: experiment CSVs, E5 load cases, E2 time-to-accuracy,
  `numbers.json`.
- `src/cli/commands/StudyCommand.*` and `src/cli/platform/Process*`: `study plan | run | batch |
  collect`; Job Object memory cap and timeout kill, performance-core pinning, keep-awake, CPU
  load, resume, dirty-tree refusal.
- `src/core/solvers/direct/Cholesky.cpp`: singularity detection (pivot ratio, solve residual).
- Tests: `tests/unit/study/StudyTest.cpp` (8), `tests/integration/study_smoke.cmake` (batch +
  resume), a direct-solver test. `docs/v2_experiments.md` (harness and runbook).

### My edits
- None to the code yet; this phase is for your review. The official overnight batch is not run.

### Verification (tests run, numbers checked, manual checks)
- Windows MSVC, warnings as errors: Release and Debug clean, **406/406 tests pass in both**.
- The smoke batch (T0) runs 4 child processes, writes 4 converged rows, collects them, and a
  second batch executes nothing (resume).
- Dry run of the whole dry plan (85 runs, `build/study_dry`, not committed): every run left a
  result; 79 converged; the singular case E8 is detected by all six solvers after the fix below;
  stand-alone AMG stagnates on HS-7 at 0.794 mm and at h = 5 (the TIM weakness of V2-7).
- **Not run:** the official batch (needs the machine idle overnight, D12) and Linux CI.

### Incorrect / unsuitable suggestions and how they were caught
- **Direct solver silently accepted a singular system.** In the dry run, both direct solvers
  reported `Converged` on the all-insulated HS-7 (K singular) with peak temperatures of 2e13 °C,
  a relative residual of 2.1 and a 100 % energy imbalance: rounding left the last pivot slightly
  positive, so the V2-5 test `pivot ≤ 0` did not fire. Fixed with a relative pivot test
  (pivot < 1e-13 of its diagonal) and a residual check after the solve (> 1e-6 → Breakdown);
  pinned by `DirectCholesky.NumericallySingularMatrixIsABreakdownNotAnAnswer`. H9 now holds for
  all six solvers. This is exactly what the dry run was for.
- **Observer-overhead check inconclusive.** The pair with and without the error observer differed
  by 9.5 %, which is process-to-process noise (only one of five repetitions carries the observer),
  not a measured observer cost; N4 is left for the idle official batch.
- **One commit per phase is not possible for V2-8:** the official batch refuses a dirty tree and
  records the commit it measured, so the harness must be committed before the runs; the results
  become a second commit, which gets the `v2-phase-8` tag.
- Smaller slips caught by tests or builds: the dry plan's first run (E1) lacked the dry-run cap;
  the T0 test compared a 5-character prefix of 188.199999… ; a shell command with nested quoting
  failed to parse and was split into separate steps; the README test filter `Study` also matched
  v1 tests and was narrowed to the 117 V2 tests.

---

## 2026-11-23: Official batch (Phase V2-8, part 2)

### Prompts (representative)
1. "push v2-8 harness and start the overnight batch, ill be off to sleep, so once the run is
   done, make necessary changes and start the next phase and push that also".

### Outputs (files, functions)
- `results/v2/` from the unattended batch on commit `5e9bea7` (241 runs).
- `docs/v2_experiments.md`: official-batch section, first reading of H1–H9 and N4.
- `src/cli/commands/StudyCommand.cpp`: `batch.log` and final `progress.json` fix;
  `tests/integration/study_smoke.cmake` checks both.

### My edits
- None yet; the user was asleep and reviews this phase afterwards.

### Verification (tests run, numbers checked, manual checks)
- Every row records commit `5e9bea7`; 241 rows, no crash, time cap or memory-cap kill.
- Hypothesis table checked against the CSVs by a script (slopes by least squares on log–log).
- Full Windows test suite in Release and Debug before the commit.

### Incorrect / unsuitable suggestions and how they were caught
- **AMG strength threshold unsuitable for trilinear hexahedra (suspected).** The V2-7 θ₀ = 0.08
  was tuned on a 7-point finite-difference Laplacian. On Q1 hexahedra of aspect ratio 1 every
  coupling is below 0.08 of the diagonal, so nodes are left out of the coarse space; the
  official batch shows stand-alone AMG stagnating even on the cube and AMG-PCG iterations
  growing with refinement. Caught by reading the official results, not by a test. Official
  results kept as measured; the fix is checked as a candidate and needs the user's decision.
- **Harness log bug.** `batch.log` kept one line because the old file was open during the
  atomic rename on Windows; the dry run did not show it because nobody read the log. Fixed and
  pinned by the smoke test.

## 2026-11-23: Report (Phase V2-10)

### Prompts (representative)
1. Relayed by the planning session: "continue to V2-10 after CI is green", with the rules: a
   single PDF with no source code and with pictures; every number traceable to a CSV; resolve
   all estimate and verify markers; leave the effort-log hours to the user; stop for review
   before tagging the final release or pushing to `main`.
2. The user, in this session: "start V2-10 once CI is green".
3. After the first draft (17 pages, below the user's D-16 range of 18–25): "Add content".

### Outputs (files, functions)
- `docs/report_v2/report.src.html` (template), `report.html` (filled), `UniForge_V2_Report.pdf`,
  `provenance.csv`.
- `tools/report_v2/fill_numbers.py` (placeholders `run`, `sup`, `e5`, `tta`, `ratio`, `diff`,
  `slope`, `status`, `numbers`, `fig`, `table`, `provenance`), `check_pdf.py`, `build_report_v2.ps1`.
- `Figures.cpp`: failure labels once per reason; fin-theory note cites Bergman et al. 2011.
- Marker resolution in the Build Spec, `figure_tokens.md`, README, code comments and two test
  comments; notes on the three historical design documents.

### My edits
- None yet; the report is for the user's review.

### Verification (tests run, numbers checked, manual checks)
- `check_pdf.py` passes: 22 pages, 2.1 MB, no placeholder, marker, code-like text, e-mail
  address or absolute path in the extracted text.
- Every number comes from `fill_numbers.py`; it fails on a missing row, column or non-number.
  Spot checks of the prose against the CSVs (E0 winners, E2 times and slopes, E3 ratios, E5 load
  cases, E6 threads).
- References: nine checked on Crossref (title, authors, journal, volume, pages, year, DOI), the
  heat-transfer textbook on Open Library (7th ed., Wiley 2011), the Okabe–Ito RGB values on the
  source's Fig. 16.
- The report was rendered and read page by page; P11 and P12 re-rendered after the label fix.
- Windows MSVC: 422/422 tests pass in Release and Debug.

### Incorrect / unsuitable suggestions and how they were caught
- **Citations for numbers they were not checked against:** the first draft attributed the
  material conductivities and h = 25 W/(m²·K) to the heat-transfer textbook without checking its
  tables. Caught while resolving the verify markers; the report now calls them chosen design
  inputs and cites the book only for fin theory.
- **Wrong figure reference:** the summary pointed to "Figure 16" for the decision chart (it is
  Figure 25). Caught when reading the rendered report.
- **Overlapping failure labels** in P11 and P12 (one label per refused size); two fixes were
  needed before the labels cleared the curves. Caught by eye on the rendered figures.
- **Section and table numbers in citations** (Saad §6.7.3, Davis ch. 4) could not be checked
  without the books; they were removed and only the verified bibliographic data kept. The
  attribution of the 4 × 4 IC(0) counter-example to Kershaw is marked as not checked against
  the paper.
- **Page count:** the first draft had 17 pages and I proposed lowering the limit rather than
  padding; the user chose to add content. Added from existing results only: thermal results
  along the design ladder, cross-solver accuracy, measured scaling exponents and work units,
  time to an engineering answer, the ordering comparison (E7) and a hypotheses section.
- **Two overstatements in the added prose,** caught by checking the filled values: "the direct
  solver's solve grows slowest" (its solve exponent is 1.27–1.48, above multigrid's; only its
  absolute solve time is small) and "four hypotheses confirmed" (three, counting H6 as
  supported). Both corrected before the commit.

### Review fixes (same phase, after the user's review of `a466125`)
- **Prompt:** the user reviewed the report and said "Send all fixes"; the planning session relayed
  nine items (five factual, four presentation).
- **Wrong or overstated statements I had written,** caught by the user's review, not by me:
  1. Appendix B said every phase was "reviewed and approved before it was committed" and that the
     author "chose" the problem and decisions. In fact I committed each phase and the user reviewed
     it before the push; the design defaults were proposed and then approved. Reworded.
  2. Appendix A said "Linux CI green" although V2-10 changed figure code and had no CI run. Now
     worded for the tagged release commit; it is true only once that tag passes CI.
  3. The scenario table said PCG-IC0 had "no breakdown seen anywhere", contradicting Figure 24
     (PCG-IC0 reports a breakdown on the singular E8 case). Now "no IC(0) pivot breakdown".
  4. E3 cited Figures 20 and 21 for the direct solver's cost, which neither figure shows. Now
     cites the Appendix D table with the measured range and notes that the TIM variants change
     the DOF.
  5. E4 said five designs; Figure 22 shows six.
- **Presentation:** half-empty pages came from forced page breaks and side-by-side plots whose
  text printed at about 3.6 pt; plots are now full width, page breaks only where needed, and the
  provenance table is two columns. Page fill was measured from the PDF's content streams
  (text and image positions) because no PDF renderer is installed. A `pip download pymupdf`
  probe I ran while looking for one failed and installed nothing; I did not pursue an install
  without asking.
- **Number without a source** caught on rereading: the first caption of the new Figure 13b said
  "grew 17×"; it now uses the two placeholders from the superseded rows.
- Verification: `check_pdf.py` passes (22 pages, 2.1 MB); 424/424 tests pass in Release and
  Debug; P11, P12, P21, P22, P23, P24 re-rendered and inspected.

## 2026-11-23: Fixes after V2-9 (Phase V2-9.1)

### Prompts (representative)
1. The user's decision, relayed by the planning session: "PNGs + adopt 0.02": write renders as
   separate PNG files and leave the scanner unchanged; adopt θ₀ = 0.02, re-run the 72 AMG rows
   officially, regenerate the figures, and document the flaw, how it was found and how it was
   fixed with before/after numbers. Commit phase by phase, new tag, CI green.

### Outputs (files, functions)
- `SvgCanvas::image/str/save`: renders saved as `<stem>-<k>.png` and linked by relative href.
- `SolverOptions::amgStrengthThreshold` = 0.02; `tests/unit/solvers/ModelProblems.hpp`
  `q1Laplacian3d`; `Amg.DefaultThresholdHandlesTrilinearHexahedra`.
- 72 rows of `results/v2/runs` and `history` re-run; `results/v2_superseded/amg_theta008/`
  with the old rows and a before/after CSV; `docs/v2_experiments.md` rewritten from "Official
  batch" on; figure text in P13, P16 and P21 updated.

### My edits
- None yet; for the user's review.

### Verification (tests run, numbers checked, manual checks)
- Windows MSVC, warnings as errors: 422/422 tests pass in Release and Debug.
- Every number in the new hypothesis table and the before/after table was recomputed from
  `results/v2/csv/*.csv` and the compare CSV by a script, not copied from the candidate run.
- All 72 re-run rows record commit `9be2090`; figures re-rendered and inspected (P1, P9, P11,
  P13a, P16, P21); no SVG contains base64.

### Incorrect / unsuitable suggestions and how they were caught
- **θ₀ = 0.08 itself (V2-7):** tuned on a 7-point Laplacian, wrong for the Q1 matrices of the
  study; caught by the official batch (AMG stagnating on the plain cube), see the experiments doc.
- **Re-run without reconfiguring:** my first re-run skipped the runbook's reconfigure step, so
  the rows recorded the stale configure-time commit `68a4041`. Caught by checking the
  `git_commit` column; the rows were discarded and the runs repeated.
- **Wrong H1 timing claim in V2-8:** I wrote that direct wins only the 10 k DOF bar with ND
  (0.029 s); direct RCM was faster there (6.5 ms) and also won the 1.8 k bar. Caught when
  recomputing H1 from E0.csv.
- **Tight test bound:** the old mesh-independence test failed at the new default on the 7-point
  Laplacian (15 → 20 cycles, bound +4); it now runs at the θ₀ it was tuned for, and the default
  is tested on the Q1 matrix instead of loosening the bound.

## 2026-11-23: Figures (Phase V2-9)

### Prompts (representative)
1. Same session as the official batch: "... once the run is done, make necessary changes and
   start the next phase and push that also" (the user was asleep; no review between phases).

### Outputs (files, functions)
- `src/core/viz/`: `Image`, `Colormap` (inferno fit as in v1, categorical), `Raster3D`
  (`Camera::fit`, `render`), `Spy` (`renderSpy`), `SvgCanvas` (`base64`, `colorbar`, `arrow`),
  `PngWriter` (moved from `app/render`; `zlibCompress` with fixed-Huffman deflate).
- `src/core/common/SvgPlot.*`: hollow/cross markers, annotations, note, `powerOfTenLabel`,
  `renderSvgBars`.
- `src/core/study/Figures.*` and `uniforge_cli study figures`; `docs/report_v2/fig/` (28 files
  and `figures.md`).
- Tests: `tests/unit/viz/VizTest.cpp` (13), `tests/unit/viz/PngWriterTest.cpp` (moved, now
  with an independent inflater), `tests/integration/study_figures.cmake`.
- Candidate AMG check (θ₀ = 0.02) on 72 runs in `build/amg_cand`, summarised in
  `docs/v2_experiments.md`; not committed as results.

### My edits
- None yet; the phase was built while the user slept and is for their review.

### Verification (tests run, numbers checked, manual checks)
- Windows MSVC, warnings as errors: Release and Debug clean, **421/421 tests pass in both**.
- Every figure rasterised with headless Edge and inspected by eye; layout fixes made for P1, P2,
  P3, P5, P11, P12, P14, P16, P17 and P21 after the first look.
- PNG output checked by an inflater written in the test (stored and fixed-Huffman blocks) and by
  undoing the row filters back to the exact pixels.
- Linux CI not run yet (runs on push of the tag).

### Incorrect / unsuitable suggestions and how they were caught
- **A placeholder line that allocated memory** (`*new Vec3[1]{}`) was written into the
  rasteriser's segment pass while drafting; replaced by the real depth-tested brush before the
  first build.
- **Changed aggregate initialisation order:** adding `hollow` before `strokeWidth` in
  `PlotSeries` broke v1's brace-initialised series (MSVC narrowing errors in
  `BenchmarkReportIO.cpp`); the field was moved to the end.
- **Figure details read from ASCII result rows:** the CSV sanitiser had turned `pᵀKp` into
  `p?Kp`; P21 restores it for display.
- **AMG explanation refined by P9:** the first wording said every coupling is weak; P9 showed
  only 0.6 % isolated nodes on HS-7 at 1 mm (stretched cells keep some strong couplings). The
  note now says this holds for cubic cells, and the candidate table states what was measured.

## 2026-11-27: Literature survey and V3a (single-precision AMG)
### Prompts (representative)
- After the interview: survey all solvers, explain why V2 used three, find competitors that may
  beat AMG-PCG, survey aerospace CFD and datasets (with four links supplied by me).
- "Yes, start V3a": implement and measure the first candidate.
### Outputs (files, functions)
- Survey page (outside the repo), with sources and unverified items marked.
- `AmgHierarchy::buildSinglePrecision`, `cycleSingle`, float kernels in `Amg.cpp`; option
  `amgSinglePrecision`; kind `AMG-PCG-SP`; experiment `V3A` in `Plan.cpp`; two tests;
  `docs/v3_experiments.md`.
### My edits
- Chose to run the mixed-precision test first, from the proposed test order.
### Verification (tests run, numbers checked, manual checks)
- 426 tests pass (Windows Release). 36 V3A runs converged on commit `6abcfe9`. Every
  percentage in `docs/v3_experiments.md` was recomputed from `results/v3/csv/V3A.csv`.
### Incorrect / unsuitable suggestions and how they were caught
- **Stale build hash again:** the first V3A pass ran a binary configured before the commit and
  recorded `4874d37`. Caught in the result table, then the build was reconfigured and all runs
  were repeated. The discarded rows are not in the repository.
- **Expected speed-up not reached:** the survey's 1.1–1.6× did not carry over (≤ 5.7 % on one
  core); recorded as a negative result instead of tuning until it looked better.
- **Placeholder in the changelog draft** ("428 → 426?") was replaced with the measured count
  before committing.

## 2026-11-27: V3b (Chebyshev–Jacobi smoother)
### Prompts (representative)
- "Yes, start V3b": the parallel-smoother test proposed after V3a.
### Outputs (files, functions)
- `chebyshev<T>`, `chebyshevBytes`, `AmgHierarchy::buildChebyshev` in `Amg.cpp`; options
  `amgSmoother`, `amgChebyshevDegree`, `amgChebyshevRatio`; kinds `AMG-PCG-CH`,
  `AMG-PCG-CH-SP`; experiment `V3B`; one test; V3B section of `docs/v3_experiments.md`.
### My edits
- Approved the step; the smoother choice (Chebyshev over multicolour Gauss–Seidel) followed the
  proposal.
### Verification (tests run, numbers checked, manual checks)
- 427 tests pass. The binary was checked to embed the commit hash `ecbf093` before the batch.
  40 V3B runs converged. Every ratio in the write-up was recomputed by hand from
  `results/v3/csv/V3B.csv` (a scripted ratio attempt failed on a key mismatch and was not used).
### Incorrect / unsuitable suggestions and how they were caught
- None found in the results. The write-up states that Chebyshev is slower on one core and that
  the total-time gain is smaller than the solve-time gain.

## 2026-11-27: V3c (parallel AMG setup)
### Prompts (representative)
- "Yes, parallel setup".
### Outputs (files, functions)
- `sparse::multiply(KernelPool&, …)`; pool versions of `strongConnections`,
  `smoothedProlongator`, `buildChebyshev` and the float copy; per-step setup timers;
  option `amgChebyshevPowerIterations`; experiment `V3C`; two tests; V3C section of
  `docs/v3_experiments.md`.
### My edits
- Approved the step.
### Verification (tests run, numbers checked, manual checks)
- 429 tests pass in Release; the AMG and sparse tests also pass in Debug. The binary embedded
  `93f65d6` before the batch. 32 V3C runs converged. The speed-ups were computed by script from
  `results/v3/csv/V3C.csv` and spot-checked by hand.
### Incorrect / unsuitable suggestions and how they were caught
- **Fewer power iterations (10)** looked like a free 0.26 s. A trial run converged in an
  impossible 4 iterations, and the run file showed Breakdown with true residual 3.1, so the
  change was reverted. The first glance at the trial output missed the status column. Caught
  because 4 iterations was implausible.
- **Unspecified argument order in a new test:** `tentativeProlongator(aggregate(A, s, count),
  count)` let MSVC read `count` before `aggregate` set it (access violation). Found with the Debug
  build's bounds assertion and fixed by splitting the statement.
- **Unexplained timing difference** (3.76 M, 8 threads: 3.38 s vs 4.85 s in V3B) is reported as
  open, not explained away.

## 2026-11-27: V3d (geometric multigrid)
### Prompts (representative)
- "Geometric multigrid".
### Outputs (files, functions)
- `geometricProlongator`, the geometric branch of `AmgHierarchy::build`, `Coarsening`,
  `Preconditioner::setHint`, hint plumbing in `Pcg::setup` and the study runner, kinds
  `GMG-PCG` and `GMG-PCG-CH-SP`, experiment `V3D`, two tests, V3D section of
  `docs/v3_experiments.md`.
### My edits
- Approved the step.
### Verification (tests run, numbers checked, manual checks)
- 431 tests pass. Smoke runs on HS-7 at 1 mm and 0.5 mm before the batch. The binary embedded
  `cea14c0`. 40 V3D runs converged. Ratios computed by script from `results/v3/csv/V3D.csv`.
### Incorrect / unsuitable suggestions and how they were caught
- **First draft of the verdict** called the 20-thread result "a tie within ±7 % up to 0.17 M".
  The table shows AMG 15 % faster at 56 k, so the sentence was corrected. The draft also said
  memory was "within 3 %" when the largest difference is 5.8 % (0.29 M), and its error
  sentence was muddled; both were rewritten from the CSV.
- **Scope stated plainly:** this GMG is matrix-based, so the literature's matrix-free advantage
  was not tested, and the write-up says so.

## 2026-11-27: Machine check and V3e (load cases in lockstep, pinned threads)
### Prompts (representative)
- "next is multi-load reuse and is my pc at optimal state of the test runs … do we need to make
  any changes for it?"
### Outputs (files, functions)
- Machine check (CPU, power plan, RAM part number and speed, background load). Three
  decisions put to the user.
- `solveMany`, `AmgBatchPcg`, block kernels and `vcycleBlock` in `Amg.cpp`;
  `performanceFirstCpus` and `pinThreadToCpu`; runner load cases; experiment `V3E`; one test;
  V3E section of `docs/v3_experiments.md`.
### My edits
- Decided: keep the RAM at 4800 MT/s (XMP off) for comparability; High performance power plan
  during batches only; pin threads and add 6- and 12-thread runs.
### Verification (tests run, numbers checked, manual checks)
- 432 tests pass. Smoke runs on HS-7 before the batch (pinned CPU recorded, lockstep faster at
  94 k). The binary embedded `07b86c4`. 54 V3E runs converged. Ratios computed by hand from
  `results/v3/csv/V3E.csv`.
### Incorrect / unsuitable suggestions and how they were caught
- **Power plan not restored:** the batch command switched back to Balanced through the Bash
  tool, which did not take effect; the plan stayed on High performance. A check after the
  batch caught it, and Balanced was restored with PowerShell.
- **Shell heredoc failed to parse** (a quote inside the C++ text); nothing was written. The code
  went through a scratch file instead.
- **Draft verdict errors fixed before committing:** the L = 1 overhead was given as 9–31 %
  (measured 10–31 %). The direct-solver break-even was given as 240–300 loads (measured about
  220 and 700). The draft also called "direct pays off for many loads" a V2 result, but V2's H5
  had refuted it.

## 2026-11-27/28: V3f (hypre and PETSc references)
### Prompts (representative)
- "hypre/PETSc reference"; separately approved: download hypre, install WSL Ubuntu and PETSc.
### Outputs (files, functions)
- `HyprePcg` (`src/core/solvers/external/`), CMake option `UNIFORGE_WITH_HYPRE`, `exportPetsc`
  and `study export`, `tools/petsc_ref/` (driver, makefile, script), experiment `V3F`, two
  tests, the V3F section of `docs/v3_experiments.md`, DEPENDENCIES.md entries.
### My edits
- Approved the hypre download and the WSL Ubuntu + PETSc install (no MPI, root without a user
  account).
### Verification (tests run, numbers checked, manual checks)
- Tests pass in both builds (434 default; 409 with hypre, no GUI). hypre agrees with our
  AMG-PCG on the Q1 model problem. PETSc's install check passed. 56 V3F runs and 16 PETSc runs
  converged. Ratios computed by hand from `V3F.csv` and `V3F_petsc.csv`.
### Incorrect / unsuitable suggestions and how they were caught
- **Runtime mismatch:** hypre was first built with the DLL runtime (`/MD`), which cannot link
  with UniForge's `/MT`. Caught before linking and rebuilt.
- **OpenMP mismatch:** MSVC's OpenMP 2.0 lacks `omp_get_thread_limit` (linker error). Fixed
  with a documented shim instead of changing the OpenMP runtime.
- **`find_package(OpenMP COMPONENTS C)`** failed in a C++-only project; fixed by enabling C
  when hypre is on.
- **Draft accuracy claim corrected:** the draft said every external error was "within the same
  order of magnitude" as ours, but hypre θ 0.5 at 1.87 M is 9× larger (6.4e-10 vs 6.7e-11 K).
  The draft's speed-up ranges were also restated per comparison.
- **Caveats recorded:** the external settings were not tuned, hypre's OpenMP threads were not
  pinned, and PETSc ran in a VM while Windows was on the Balanced plan.

## 2026-11-28: V3g (sanitizer check, timing repeat)
### Prompts (representative)
- "Yes, do both": run the CI's ASan+UBSan and TSan builds of v3 locally without pushing, and
  repeat the 3.76 M / 8-thread timing to explain the V3B vs V3C gap.
### Outputs (files, functions)
- GCC compile fixes in `Amg.cpp`, `Runner.cpp`, `HypreSolver.cpp`, `AmgTest.cpp`.
- `UNIFORGE_NO_PIN` switch in `StudyCommand.cpp`, `tools/timing_repeat_v3g.sh`, the V3G write-up
  and `results/v3/csv/V3G_timing.csv`.
### My edits
- Approved the High performance plan for the timing batch only (restored to Balanced after).
### Verification (tests run, numbers checked, manual checks)
- ASan+UBSan and TSan: 409/409 each, no reports; Windows 434/434 after the fix. Every timing
  row was taken from the run CSVs. The old binaries were rebuilt from the V3B/V3C commits.
### Incorrect / unsuitable suggestions and how they were caught
- **Hypothesis disproved:** the earlier note attributed the gap to the λ̂ start vector or the
  setup change. The repeat showed V3B's own binary in the fast cluster, so the cause is thread
  placement.
- **Draft corrected:** the first V3G draft proposed "one thread per P-core first" as a fix, but
  the pinning code already does that; the draft was corrected after reading the code.
- **False alarm:** a grep for `error:` in the TSan build log matched `system_error` include
  lines, not errors.

## 2026-11-28: V3h (thread runs re-timed pinned)
### Prompts (representative)
- "re-time V3B–V3D thread runs pinned": rerun every multi-threaded V3B–V3D row with pinning,
  keep the old CSVs, report whether any verdict changes.
### Outputs (files, functions)
- `planV3H` in `Plan.cpp` (56 runs), `results/v3/csv/V3H.csv`, the V3H section and the pointers
  in the V3B–V3D verdicts.
### My edits
- None to the code; asked for the re-timing after V3G.
### Verification (tests run, numbers checked, manual checks)
- 434 tests pass. All 56 runs converged, at commit `bec0817` (hash checked in the binary). Every
  ratio was computed by script from `V3H.csv` and the old CSVs, then re-checked by hand.
### Incorrect / unsuitable suggestions and how they were caught
- **Draft claims corrected before commit:** the first V3H draft said every other run spread
  ≤ 10 %, but 10 of 12 small-mesh runs had outliers. It also miscounted the runs (24/16 instead
  of 28/12) and misstated three ranges (pinned-vs-V3C slowdown, V3B slow rows, 8 vs 20
  threads). All were caught by recomputing from the CSV.

## Entry template

```markdown
## YYYY-MM-DD: <phase / topic>
### Prompts (representative)
### Outputs (files, functions)
### My edits
### Verification (tests run, numbers checked, manual checks)
### Incorrect / unsuitable suggestions and how they were caught
```
