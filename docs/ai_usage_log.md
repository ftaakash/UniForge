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
- _To fill after review._

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
- _To fill after review._

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
