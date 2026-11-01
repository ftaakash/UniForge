# UniForge — Analysis & Development Plan

**Assignment:** PASS SCIENCE WORKS R&D Internship 2026–27, Unified Engineering Workbench (100 pts)
**Repo:** https://github.com/ftaakash/UniForge (public, currently empty)
**Goal:** ≥ 95/100, all shortlist gates passed with margin
**Status:** PLAN ONLY. Nothing built yet. To be compared against two system-design skill outputs before execution.

---

## 0. Findings before planning

### 0.1 Gaps between the workflow `.md` and the official PDF
The `.md` is a good summary but misses items the PDF (the source of truth) requires or the rubric scores:

| # | PDF requirement missing/weak in the `.md` | Where it costs marks |
|---|---|---|
| 1 | Task E: **"Include a safe integration plan and limitations."** | Embedded (8) |
| 2 | **Record actual effort** (not just target 40–50 h) | Delivery/report |
| 3 | Build/packaging scripts **must not implement backend features** | Gate risk if logic leaks into scripts |
| 4 | Task D: **"Explain overhead and limitations"** | Multicore honest analysis (2) |
| 5 | Thermal FEA scored as **6 formulation/assembly/BCs + 5 verification/convergence + 3 assumptions & learning explanation** ("interdisciplinary adoption") | FEA (14) |
| 6 | Version control scored on **"sensible exclusions and dependency/version records"**; "commit count alone earns no credit" | VC (5) |
| 7 | **Verification discussion**: evaluator may ask you to explain any function, reproduce a test, and make one small change live | Any score can be revised |
| 8 | Scoring anchors: 0 / 25 / 50 / 75 / 100 % per sub-criterion; 100 % needs "complete, correct **and supported by reproducible evidence**" | Everything |
| 9 | Interface quality = **usability and clarity**, not polish; "usable plots" | Interface (8) |
| 10 | AI log must contain **at least one incorrect/unsuitable suggestion identified** (or a deliberate challenge test); **do not fabricate interactions** | AI judgement (8) |

**Implication of #8:** every feature needs a *reproducible evidence artefact* (test, CSV, log, screenshot) or it caps at 75 %. The plan below pairs every requirement with its evidence.

### 0.2 Numerical targets independently re-checked
- FEA: `T_exact = 100 + 420x − 500x²`, `T_max = 188.2 °C at x = 0.42 m`. With linear elements + consistent load, 1D FE is **nodally exact** for this problem, so midpoint error = pure interpolation error = `qh²/(8k)` → 1.25, 0.3125, 0.078125, 0.01953125 °C. Ratio exactly 4. Tests can assert nodal error ≈ 1e-12 *and* midpoint error = formula to ~1e-10 (strong, defensible test).
- Backprop: `h = tanh(0.5) = 0.462117`, `e = −1.537883`, `dc = e`, `dv = e·h = −0.710682`, `dw = db = e·v·(1−h²)·x = −1.209464`; updates match the `.md`. ✔

### 0.3 Environment audit (this machine)
| Item | Found | Consequence |
|---|---|---|
| CPU | i5-14600K, 14 cores / 20 logical (P+E hybrid) | 1/2/4 workers fine; hybrid cores must be mentioned in perf analysis |
| RAM / disk | 31.8 GB / 36.9 GB free | OK |
| C++ compiler, CMake, Ninja, Qt | **none installed** | Toolchain install is step 1 (needs your OK — ~4–6 GB download) |
| git identity | the repository owner's git identity (`ftaakash`) | Commits will use this; no Claude co-author lines |
| gh CLI | installed | Push/PR possible |
| GitHub repo | exists, empty, public | We seed it in Phase 0 |

---

## 1. Scoring strategy — rubric → evidence → target

| Criterion (max) | What earns 100 % | Our evidence artefact | Target |
|---|---|---|---|
| Interface (8) | Clear workflow, readable layout, responsive status, validation, usable plots | 4 (+1) views, global status bar + log panel, inline validation, progress/cancel, ImPlot plots with units, screenshots in demo | 7.5–8 |
| C++ backend & testing (12) | Integration, separation of concerns, robust error handling, positive/negative/numerical tests | `uniforge_core` lib with zero UI deps, `Result<T>`/error types, ~80+ GoogleTest cases labelled unit/numerical/negative/integration, CI on Windows + Linux (ASan/TSan) | 11.5–12 |
| Folder architecture (6) | Clean modules, relative paths, controlled deps | Layout in §3, vendored deps with `DEPENDENCIES.md` (version, licence, SHA) | 6 |
| Readability (6) | Naming, focused functions, style, maintainable interfaces | `.clang-format`, `.clang-tidy`, small headers with doc comments on contracts only | 5.5–6 |
| Version control (5) | Traceable meaningful history, exclusions, dep records | Phase-by-phase commits + tags, `.gitignore`/`.gitattributes`, `DEPENDENCIES.md`, CHANGELOG, git bundle | 5 |
| Thermal FEA (14) | 6 formulation + 5 verification + 3 learning | Solver + exact-value tests + convergence CSV/plots + `docs/thermal_fea.md` with derivation, BC method, **genuine** debugging insight | 13.5–14 |
| CAD/ANSYS viz (10) | Both imports, units/legend, interaction, provenance, malformed handling | STL + CSV importers, D3D11 viewer, picking, legend, `data/PROVENANCE.md`, negative tests, measured size limits | 9.5–10 |
| Multicore (10) | 4 real parallel + 4 reproducible timing/agreement + 2 honest analysis | Bounded `std::thread` pool, CLI benchmark w/ warm-up + 5 reps + median, JSON/CSV reports, max-diff check, TSan CI, `docs/performance.md` | 9.5–10 |
| Embedded (8) | 4 design + 4 working mocks w/ recovery | Block diagram (SVG), pin/voltage table from official docs, safe integration plan, 3 mock adapters with timeout→recover in UI | 7.5–8 |
| Backprop (7) | 4 handwritten + 3 C++ grad-check & loss plot | **Your** handwritten 2 pages; C++ module + 4 FD checks + 100-step loss CSV/plot + explanation | 6.5–7 |
| AI judgement (8) | Clear prompts, iteration, independent verification, disclosure, critical judgement | `docs/ai_usage_log.md` kept **live** each phase; real rejected suggestions logged with evidence | 7–8 |
| Delivery/report (6) | 3 clean-machine launch + editor config; 3 evidence/report/limitations | Launch.exe tested in Windows Sandbox (clean), `.vscode` config, 6-page report | 6 |
| **Total** | | | **~95–99** |

The weakest points are the ones I can't fully control: the handwritten appendix and the AI log both depend on your input, and interface/embedded scores depend on the evaluator's judgement. Everything else can be locked down with tests.

---

## 2. Technology decision

### 2.1 Recommended stack
| Layer | Choice | Why |
|---|---|---|
| Language | **C++17** (MSVC 19.4x) | Exactly what the brief says; nothing to argue over |
| Build | CMake ≥ 3.25 + Ninja, `CMakePresets.json` | One-command reproducible builds; presets = documented configs |
| UI | **Dear ImGui (docking) + ImPlot**, vendored, MIT | Tiny, static, immediate-mode makes responsiveness/progress trivial; plots with units built in |
| Graphics backend | **Win32 + Direct3D 11** (+ `d3dcompiler_47.dll`, which ships with Win10/11) | **Key risk finding:** a clean Win11 VM/Sandbox without GPU driver often exposes only OpenGL 1.1, so an OpenGL viewer can fail the launch gate. D3D11 always works (WARP software fallback). |
| File dialogs | Native `IFileOpenDialog` wrapper | No extra dependency |
| Tests | GoogleTest (vendored) + CTest labels | Standard, CI-friendly |
| CRT | Static `/MT` | **No VC++ redistributable needed** → single self-contained exe, no DLLs to bundle |
| Plots for report | Small C++ SVG writer in the CLI tool | Deterministic, reproducible, no Python dependency |
| CI | GitHub Actions: Windows (MSVC build + tests + package) and Linux (clang, ASan + **TSan** on core tests) | Evidence for robustness and concurrency correctness; core stays portable |

### 2.2 Alternatives considered (for the comparison with the system-design outputs)
| Option | Pros | Cons | Verdict |
|---|---|---|---|
| Qt 6 Widgets + QOpenGLWidget/VTK | Native widgets look polished; the `.md` suggests it | ~5–10 GB toolchain, windeployqt DLL bundle, LGPL compliance work, OpenGL clean-VM risk, VTK adds a huge dependency | Viable, but higher packaging risk for no rubric gain ("no preference for a framework") |
| Web UI (Electron/Tauri) + C++ backend process | Easy pretty UI | Process/transport layer, IPC failure modes, larger bundle, "manually starting backend" risk | Reject |
| ImGui + GLFW + OpenGL | Simplest code | OpenGL 1.1 on clean VMs | Reject in favour of D3D11 |

### 2.3 Toolchain to install (needs your approval — large download)
- Visual Studio 2022 **Build Tools** (workload: Desktop C++, MSVC v143, Windows 11 SDK, CMake + Ninja components) via `winget` (~4–6 GB).
- Optional: LLVM/clang-format/clang-tidy (or the VS components).
- Optional for clean-machine testing: **Windows Sandbox** feature (Win11 Pro supports it; enabling it is a system setting you must turn on yourself).

---

## 3. Architecture

### 3.1 Principles
1. `uniforge_core` is a **static library with zero UI/OS-graphics dependencies**. It takes inputs, returns structured results or typed errors. Everything evaluated as "backend" lives here.
2. Frontend (`uniforge_app`) only renders, collects input, and dispatches jobs.
3. One **job runner** service in the app: every long operation runs on a worker thread, reports `progress(0..1, message)`, honours a cancellation token, and **publishes results atomically** (result swapped in only on success, so cancelling never corrupts the result).
4. A **headless CLI** (`uniforge_cli`) calls the same core to regenerate every evidence file in `results/`. That gives reproducible evidence with one command.
5. All paths are relative to the executable directory, which is resolved at startup.

### 3.2 Repository layout
```text
UniForge/
├── CMakeLists.txt, CMakePresets.json, .clang-format, .clang-tidy
├── .gitignore, .gitattributes, LICENSE, README.md, CHANGELOG.md
├── cmake/                      CompilerOptions.cmake, Warnings.cmake
├── third_party/                imgui/, implot/, googletest/  + DEPENDENCIES.md (ver, licence, SHA)
├── src/
│   ├── core/                   → uniforge_core (portable C++17, no UI)
│   │   ├── common/             Result.hpp, Error.hpp, Cancellation.hpp, Progress.hpp, CsvWriter, Log
│   │   ├── thermal/            RodProblem, Mesh1D, ElementMatrices, Assembler, TridiagonalSolver,
│   │   │                       BoundaryConditions, AnalyticalSolution, ErrorAnalysis, ConvergenceStudy
│   │   ├── io/                 StlParser, ThermalCsvParser, ImportLimits, Validation
│   │   ├── perf/               BatchGenerator, WorkerPool, SerialRunner, ParallelRunner,
│   │   │                       Benchmark, SystemInfo (CPU/RAM/compiler/flags)
│   │   ├── hw/                 IDeviceAdapter, DeviceRegistry, MockTransport,
│   │   │                       McuAdapter, FpgaAdapter, JtagAdapter, DeviceStateMachine
│   │   └── ml/                 TinyNet (forward/backward/update), GradientCheck, Trainer
│   ├── app/                    → UniForge.exe (ImGui + D3D11)
│   │   ├── platform/           Win32Window, D3D11Context, FileDialog, PathUtil
│   │   ├── render/             Camera (orbit/pan/zoom/fit), MeshRenderer, PointCloudRenderer,
│   │   │                       Colormap, Picking
│   │   ├── jobs/               JobRunner (worker thread, progress, cancel, atomic publish)
│   │   ├── views/              GeometryView, ThermalView, PerformanceView, HardwareView, BackpropView
│   │   └── widgets/            StatusBar, LogPanel, Legend, ValidatedInput
│   ├── cli/                    uniforge_cli: fea | bench | backprop | gen-samples | sysinfo
│   └── launcher/               Launch.exe (tiny Win32: locate app\UniForge.exe, start it, show
│                               a message box with an actionable error if missing)
├── tests/                      unit/, numerical/, negative/, integration/, fixtures/ (bad files)
├── data/                       geometry/*.stl, thermal/*.csv, PROVENANCE.md (units, schema, licence)
├── results/                    csv/, plots/ (SVG/PNG), benchmarks/ (json+csv+md), logs/
├── docs/                       architecture.md, thermal_fea.md, performance.md, embedded_design.md,
│                               embedded_block_diagram.svg, ai_usage_log.md, effort_log.md,
│                               ownership.md, report/report.pdf, appendix_handwritten/, demo/
├── tools/                      package.ps1, make_bundle.ps1  (packaging only, no backend logic)
└── .vscode/                    launch.json ("Run packaged UniForge"), tasks.json, extensions.json
```

### 3.3 Core interfaces (sketch)
```cpp
// common
template<class T> class Result;                 // value or Error{code, message, hint}
class CancelToken { bool requested() const; };  // atomic<bool>
using ProgressFn = std::function<void(double fraction, std::string_view stage)>;

// thermal
struct RodProblem { double L, A, k, q, T0, TL; int elements; };  // validate() → Result<void>
struct FeaSolution { std::vector<double> x, T; };                // nodal
Result<FeaSolution> solveRod(const RodProblem&, const CancelToken* = nullptr);
double feInterpolate(const FeaSolution&, double x);
struct ErrorReport { double maxNodalErr, maxMidpointErr; std::vector<double> midX, midFe, midExact; };
ErrorReport analyse(const RodProblem&, const FeaSolution&);
std::vector<ConvergenceRow> convergenceStudy(const RodProblem&, std::span-like elements {10,20,40,80});

// io
Result<StlMesh>        parseAsciiStl(std::istream&, const ImportLimits&, ProgressFn, const CancelToken*);
Result<ThermalNodeSet> parseThermalCsv(std::istream&, const ImportLimits&, ProgressFn, const CancelToken*);

// perf
std::vector<RodProblem> generateBatch(uint64_t seed, int cases, int elements);  // k∈[5,50], q∈[1e3,5e4]
std::vector<CaseResult> runSerial(const std::vector<RodProblem>&);
std::vector<CaseResult> runParallel(const std::vector<RodProblem>&, int workers); // index-ordered output
BenchmarkReport benchmark(const BenchmarkConfig&);   // warm-up, 5 reps, median, speedup, efficiency, maxDiff

// hw
class IDeviceAdapter { virtual DeviceInfo info(); virtual Capabilities capabilities();
                       virtual Result<void> connect(); virtual void disconnect();
                       virtual Result<Telemetry> readTelemetry(); virtual bool isSimulated() const { return true; } };

// ml
struct Params { double w, b, v, c; }; struct Grads { double dw, db, dv, dc; };
Forward forward(Params, double x); Grads backward(Params, double x, double y);
Params sgdStep(Params, Grads, double lr); GradCheck finiteDiff(Params, double x, double y, double eps=1e-6);
```

---

## 4. Task-by-task design

### Task A — Interface & one-click delivery
- **Shell:** docked layout with a left nav (Geometry · Thermal Analysis · Performance · Hardware · Backprop), central view, **bottom log panel** (timestamped, exportable `.log`), **status bar** (current job, progress bar, Cancel button, last error).
- **Validation:** every numeric input validated live (red outline + message, Run disabled until valid, e.g. "k must be > 0 W/(m·K)").
- **Errors:** typed errors with an actionable hint: *"Line 214: expected 'vertex x y z', found 'vertx'. File appears truncated or malformed."*
- **Responsiveness:** all imports/solves/benchmarks run in `JobRunner`; UI thread never blocks. **Cancellation:** cooperative token checked inside loops; results published only on success, so the previous result stays intact.
- **Export:** CSV for every table, PNG of plots/3D viewport, log export, benchmark JSON/CSV/MD.
- **Launch:** `Launch.exe` (root) → `app\UniForge.exe`. Static CRT, no DLLs beyond Windows system ones. Writes `logs/launch.log`. **Launch-to-ready time** measured from process creation time (`GetProcessTimes`) to first presented frame, shown in the About box and logged.
- **Editor config:** `.vscode/launch.json` "Run packaged UniForge" (`cppvsdbg`, program = `${workspaceFolder}/dist/.../app/UniForge.exe`), prerequisite documented: *C/C++ extension (ms-vscode.cpptools)*. Plus a `tasks.json` fallback that needs no extension.
- **Evidence:** demo video, screenshots per view, `results/logs/launch_to_ready.txt`.

### Task B — STL & ANSYS-CSV visualisation
- **STL parser:** state machine over tokens `solid/facet normal/outer loop/vertex×3/endloop/endfacet/endsolid`; rejects missing `solid`, wrong vertex count, non-finite numbers (`nan`, `inf`, overflow), truncated file, binary STL detected → explicit "binary STL not supported" message, empty file → "File is empty (0 bytes)" vs "solid with 0 facets". Recomputes normals if they're zero; flags degenerate triangles (warn, not reject).
- **CSV parser:** exact header `node_id,x_m,y_m,z_m,temperature_C` (whitespace/BOM tolerant, CRLF ok); per-row errors with line numbers; rejects missing columns, extra columns, non-numeric, non-finite, duplicate node IDs, temperature below −273.15 °C; header-only file → "no data rows".
- **Limits:** `ImportLimits` (max bytes, max facets/nodes) enforced with a clear message. **Measured** limits documented: parse time and memory for generated 10k / 100k / 1M facet STL and 10k / 100k / 1M node CSV (table in `data/PROVENANCE.md` + report).
- **Viewer (D3D11):** orbit (LMB), pan (MMB/Shift+LMB), zoom (wheel), **Fit-to-view** button + `F` key, flat-shaded triangles + optional wireframe, axis triad, bounding-box dimensions in metres.
- **Point cloud:** instanced screen-space quads, **inferno** colormap, vertical legend with min/max/ticks in °C, click picking (CPU nearest-point to ray within pixel radius) → inspector panel: node_id, x/y/z [m], T [°C]; hover tooltip.
- **Sample data (ties the app together):**
  - `rod_10x10mm_1m.stl`: a self-created 1 m × 10 mm × 10 mm bar (A = 1e-4 m², the FEA rod), produced by `uniforge_cli gen-samples`. Self-created, licensed as part of the repo.
  - `rod_nodal_temperature_SYNTHETIC.csv`: nodes on that bar with T from the analytical solution plus a documented lateral variation. **Labelled SYNTHETIC in filename, header comment and PROVENANCE.md; not an ANSYS export.**
  - Optional second visually richer STL (e.g., finned heat sink), also self-created.
- **Tests:** valid parse counts/bounds; ~15 negative fixtures (each a file in `tests/fixtures/`), each asserting the specific error code and line.

### Task C — 1D thermal FEA (centrepiece)
- **Formulation (documented in `docs/thermal_fea.md`):** weak form → element stiffness `Ke = (kA/h)[[1,−1],[−1,1]]`, consistent load `fe = (qAh/2)[1,1]`; assembly into **tridiagonal** storage; Dirichlet BCs by **elimination preserving symmetry** (move known column to RHS, set row/col to identity). Solve with **Thomas algorithm** (O(N)); a dense Gaussian-elimination solver exists **only in tests** as an independent oracle.
- **Post-processing:** FE interpolation at element midpoints `T_mid = (T_i + T_{i+1})/2`, max |error| vs exact; also nodal error; reaction heat flux at both ends + **energy balance check** (q·A·L = flux out both ends = 1 W), which is a nice extra physics verification.
- **Outputs:** per-mesh CSV `x_m, T_fe_C, T_exact_C, abs_err_C` + midpoint CSV; `convergence.csv` (`N, h_m, max_mid_err_C, expected_qh2_8k, ratio, observed_order`); plots: T vs x for all 4 meshes + exact curve, log-log error vs h with slope-2 reference line. All axes labelled with units.
- **UI:** parameters editable with validation (defaults = brief values), mesh selector incl. "Run study (10/20/40/80)", plots, results table, export button.
- **Tests:** element matrix values; assembled K symmetric & singular before BCs (row sums = 0); BCs imposed exactly; nodal error < 1e-10; midpoint errors equal the table to 1e-9; ratio = 4 ± 1e-6; observed order = 2; energy balance; Thomas == dense oracle; invalid inputs (N < 1, k ≤ 0, non-finite) rejected.
- **Learning explanation (3 marks):** plain-language write-up: what the governing equation means physically, why the profile is parabolic with peak at 0.42 m, why linear FE is nodally exact in 1D (Green's function argument, briefly), why the error is O(h²). **Debugging insight must be genuine**: we will log real bugs as they happen in `docs/thermal_fea.md#debugging-log` (no invented story).

### Task D — Multicore
- **Batch:** `std::mt19937_64(seed)`, k ~ U[5, 50], q ~ U[1e3, 5e4], T0/TL fixed; configurable cases & mesh size. Same `solveRod` for serial and parallel.
- **Parallel:** bounded `WorkerPool` of `p` `std::thread`s pulling **chunked indices** from an atomic counter; each writes into a preallocated `results[i]` → deterministic order, no locks in the hot path, no false sharing (results padded/written once per case). Not a UI background thread: it lives in the core and is called by the CLI and the UI.
- **Benchmark protocol:** compute-only timing with `steady_clock`; 1 warm-up + **5 measured reps** per p ∈ {1, 2, 4} (capped at `hardware_concurrency`); report median, min/max, speedup T1/Tp, efficiency; **max |ΔT| serial vs parallel** over all nodes of all cases (expect exactly 0, must be ≤ 1e-8). Default workload sized so T1 ≈ 1–3 s (e.g., 2 000 cases × 2 000 elements; tuned and recorded).
- **Report:** CPU model, physical/logical cores (`GetLogicalProcessorInformationEx`), RAM, compiler + version, flags (`/O2 /MT /DNDEBUG`), workload, seed, date → `results/benchmarks/bench_<date>.{json,csv,md}`. Launch-to-ready reported separately.
- **Honest analysis:** memory-bandwidth-bound tridiagonal solve, allocation cost, thread start-up overhead, **P-core vs E-core hybrid scheduling on the 14600K**, Amdahl. Optional extra series up to 8/16 workers clearly marked as beyond the requirement.
- **UI:** config form (seed, cases, mesh, workers), Run/Cancel, live progress, table + speedup/efficiency bar chart + agreement badge.
- **Tests:** generator reproducibility (same seed → identical batch), serial == parallel bitwise, order preserved, p > cases handled, p capped, cancellation leaves no partial publish; **TSan** run in Linux CI.

### Task E — Embedded integration
- **Board selection (values to be verified from official datasheets/user manuals during Phase 6; nothing is filled in from memory):**
  - MCU: **ST NUCLEO-F446RE** (3.3 V logic, on-board ST-LINK/V2-1 providing USB VCP + SWD).
  - FPGA: **Digilent Basys 3** (Artix-7, 3.3 V Pmod I/O, USB-UART via FTDI, on-board USB-JTAG).
  - JTAG adapter: **SEGGER J-Link EDU Mini** (or Digilent JTAG-HS2) with VTref target-voltage sensing.
- **Design content:** labelled block diagram (host PC ↔ USB hub ↔ 3 devices; power/ground domains; debug vs data paths); interface/pin-voltage table (power source, ground reference, I/O voltage, level shifting needed? where, connectors, host protocol, JTAG/SWD signals TCK/TMS/TDI/TDO/nRESET/VTref and **who owns them**); explicit **"JTAG adapter is a debug/boundary-scan probe that drives another target's TAP; it is not a compute board and has no application telemetry of its own"**; **signal-ownership conflict** example (on-board ST-LINK vs external probe: jumper removal); **safe integration plan** (common ground first, power sequencing, VTref check before driving, current limits, ESD, never back-power through USB, bring-up order) + limitations.
- **Mocks:** `MockTransport` with seeded latency/jitter; discovery returns 3 devices with VID/PID-like IDs; capabilities differ per type (MCU: UART telemetry, GPIO; FPGA: bitstream-status, XADC temp; JTAG: IDCODE scan, VTref sense); state machine `Disconnected → Connecting → Connected → Faulted(Timeout) → Recovering → Connected` with bounded retries + backoff; "Inject timeout" button per device.
- **UI:** device cards with a persistent **"SIMULATED"** badge, capability list, connect/disconnect, live telemetry sparkline, fault banner with "Retry/Recovered" trail in the log.
- **Tests:** discovery set, capability reporting, connect/disconnect idempotence, timeout → recoverable error → reconnect, max retries → permanent fault message.

### Task F — Backprop
- **C++:** explicit forward/backward (no autodiff), simultaneous update from original params, central FD with ε = 1e-6 for all 4 params (|err| ≤ 1e-5 asserted), 100 further steps → `loss_curve.csv` + SVG plot; explanation (loss → 0 because 4 params fit 1 sample; monotone decrease at lr = 0.1, tanh saturation effects).
- **UI:** Backprop view showing forward values, gradient table (analytic vs FD vs |diff| with pass badges), updated params, loss plot.
- **Tests:** every value in the `.md` tables to 1e-6; FD checks; first-update params; loss non-increasing.
- **Handwritten (you):** ≤ 2 pages scanned to `docs/appendix_handwritten/`. I'll provide a checklist of what each page must contain, but **you must write it yourself**.

---

## 5. Testing strategy
| Label | Examples | Count (est.) |
|---|---|---|
| `unit` | element matrices, CSV writer, colormap, state machine, path utils | ~25 |
| `numerical` | FEA exactness/convergence/energy, Thomas vs dense, backprop values & FD, serial==parallel | ~20 |
| `negative` | 15 STL + 12 CSV bad fixtures, invalid params, size limits, non-finite | ~35 |
| `integration` | CLI end-to-end regenerates results and compares to committed golden CSVs; batch + benchmark smoke; import → render-data pipeline | ~8 |

CI: Windows (MSVC Release + tests + package artefact), Linux (clang, `-fsanitize=address,undefined` and a separate `thread` job). Test output saved as JUnit XML in `results/logs/` for the report.

---

## 6. Packaging & delivery
- `tools/package.ps1` (packaging only): configure → build Release → run ctest → stage:
```text
CandidateID_Workbench/
├── Launch.exe
├── app/UniForge.exe            (static CRT, no extra DLLs)
├── data/  results/  docs/  tests/  src/  third_party/  cmake/  tools/  .vscode/
├── CMakeLists.txt  CMakePresets.json  README.md  LICENSE  DEPENDENCIES.md
└── git/UniForge.bundle + git_log.txt
```
- **Clean-machine test:** Windows Sandbox (fresh Win11, no VC++ redist, no GPU driver) → extract ZIP → double-click Launch.exe → exercise all views → screenshot + log. Also run from a path with spaces and non-ASCII characters, and from a read-only location (app writes exports to a user-chosen folder; logs fall back to `%LOCALAPPDATA%` if dir not writable).
- **Hygiene check script:** scan the staged tree for absolute paths (`C:\Users`), emails, tokens before zipping.

---

## 7. Phase plan & commit strategy

**Commit rules (per your instruction):** all commits authored and committed as the repository owner's git identity (`ftaakash`); **no `Co-Authored-By` or any Claude attribution lines**. Message format: `phaseN(scope): imperative summary` + body explaining *why*. Several focused commits per phase (not one dump), each building and passing tests; an annotated tag at the end of each phase (`phase-1-skeleton`, …). Push to `main` after each phase once you've reviewed it.

| Phase | Scope | Exit gate (evidence) | Est. h |
|---|---|---|---|
| **0 Setup** | Toolchain install (your OK), repo seed: README stub, LICENSE, .gitignore/.gitattributes, clang-format, CMake skeleton, `docs/ai_usage_log.md`, `docs/effort_log.md` started | `cmake --preset` configures | 2 |
| **1 Walking skeleton + packaging** | Win32/D3D11/ImGui shell with 5 empty views, status bar, log panel, JobRunner, Launch.exe, package.ps1, .vscode config, CI | **ZIP launches in Windows Sandbox** (de-risks the gate on day 1) | 5 |
| **2 Thermal FEA core** | thermal module, CLI `fea`, SVG writer, tests, docs/thermal_fea.md draft | all numerical tests green; convergence CSV/plots committed | 6 |
| **3 Importers** | STL/CSV parsers, limits, fixtures, sample-data generator, PROVENANCE.md, size-limit measurements | negative suite green; limits table | 5 |
| **4 Visual views** | D3D11 mesh + point-cloud renderers, camera, picking, legend; Thermal view with plots/export | manual checklist + screenshots | 7 |
| **5 Multicore** | batch, worker pool, runners, benchmark, sysinfo, Performance view, TSan CI | agreement ≤ 1e-8, bench report committed | 5 |
| **6 Embedded** | datasheet-verified table, block diagram, safe integration plan, mocks, Hardware view | all 3 devices + timeout recovery demo | 4 |
| **7 Backprop** | ml module, tests, view, loss plot, explanation; **you** write appendix | FD checks pass; appendix scanned | 2.5 |
| **8 Hardening** | cancellation audit, error copy pass, integration tests, ASan, large-file runs, UI usability pass | full suite green on CI | 3.5 |
| **9 Delivery** | report (≤ 6 pages), README, ownership, AI log final, demo, bundle, ZIP, hygiene scan, final Sandbox test | final ZIP launch-tested | 5 |
| | | **Total** | **~45** |

---

## 8. Things only you can do
1. **Handwritten backprop appendix** (4 of 7 Task F marks): must be personally written.
2. **Approve the toolchain download** and enable **Windows Sandbox** (system setting) for clean-machine testing.
3. **Candidate ID** for the ZIP filename.
4. **Demo video** (3–5 min) or approve a timestamped screenshot walkthrough.
5. **Understand the code for the verification discussion.** The evaluator may ask you to explain a function, rerun a test and make a small change live. I'll add a `docs/walkthrough.md` with "where is X / how do I change Y" and we should do a dry run.
6. **AI-use log honesty.** Commits will carry no Claude attribution as you asked, but the brief *requires* disclosing AI assistance in the AI-use log and ownership statement (8 marks + integrity). Commit metadata and disclosure are separate things: commits under your name are fine as long as `docs/ai_usage_log.md` honestly records Claude Code use. Hiding it would risk an integrity finding, so the log stays truthful.

## 9. Key risks
| Risk | Impact | Mitigation |
|---|---|---|
| OpenGL unavailable on evaluator VM | Launch gate fail | D3D11 + WARP fallback; tested in Sandbox |
| Missing VC++ runtime on clean PC | Launch gate fail | Static `/MT`; dependency scan with `dumpbin /dependents` |
| Antivirus flags unsigned launcher | Launch friction | Plain Win32 launcher, version resource, no packing/obfuscation; documented |
| Timings noisy on hybrid CPU | Weak perf analysis | Median of 5, warm-up, report spread, discuss P/E cores |
| Over-polish eating time | Missed evidence | Phase gates, evidence-first |
| Fabrication concerns | Integrity | Synthetic data labelled; real debugging log; no invented AI interactions |

## 10. Open decisions to settle in the comparison
1. UI stack: ImGui + D3D11 (recommended) vs Qt 6.
2. Backprop as a 5th top-level view (recommended for evaluator clarity) vs a sub-tab of Thermal.
3. Board choices for Task E (Nucleo-F446RE / Basys 3 / J-Link EDU Mini).
4. Commit to `main` directly with phase tags (recommended for a solo assessment) vs a branch + PR per phase.
5. Demo format: video vs screenshot walkthrough.
