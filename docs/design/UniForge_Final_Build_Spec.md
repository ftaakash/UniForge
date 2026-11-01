# UniForge: Final Build Specification

**Project:** Unified Engineering Workbench (PASS SCIENCE WORKS R&D Internship 2026–27)
**Repo:** https://github.com/ftaakash/UniForge
**Target:** ≥ 95/100, all shortlist gates passed with margin
**Status:** Final merged spec. **Single source of truth for the build.** Nothing is built yet.

**Merged from:**
- `UniForge_Analysis_and_Development_Plan.md` (Plan): rubric strategy, phases, user-only tasks
- `UniForge_System_Design.md` (SysDesign): requirements IDs, contracts, threading, NFRs, the backbone of this spec
- `UniForge_UI_Design_System.md` (UI): audit corrections #1–#12, UI specification

All three source docs move to `docs/design/` in the repo as evidence of the design iterations and review. §17 lists exactly what changed in the merge.

---

## 0. Decisions

### 0.1 Settled
| # | Decision | Choice |
|---|---|---|
| D1 | Language | C++17, MSVC v143 (VS 2022 Build Tools) |
| D2 | UI | Dear ImGui (docking) + ImPlot, vendored |
| D3 | Graphics | Win32 + Direct3D 11, hardware → **WARP fallback**, shaders **precompiled** with fxc |
| D4 | Runtime | Static CRT `/MT`; no redistributable DLLs |
| D5 | Parallelism | Hand-written bounded `std::thread` pool (no OpenMP: it would add a runtime DLL) |
| D6 | Linear solver | Thomas algorithm; dense Gaussian elimination as test-only oracle |
| D7 | Process model | Single process, in-process core; headless CLI on the same core |
| D8 | Fonts | Inter + JetBrains Mono (SIL OFL 1.1), embedded, with Latin-1 + Greek + math glyphs |
| D9 | Tests / CI | GoogleTest + CTest labels; GitHub Actions Windows + Linux (ASan/UBSan, TSan) |
| D10 | Board set (Task E) | NUCLEO-F446RE, Digilent Basys 3, SEGGER J-Link EDU Mini, **provisional until the datasheet check in Phase 6** |
| D11 | Branching | Commit to `main`, annotated tag per phase |
| D12 | Commits | Author/committer the repository owner's git identity (`ftaakash`); **no Co-Authored-By or any Claude attribution**; format `phaseN(scope): summary` |

### 0.2 Adopted defaults (you can still change these)
| # | Decision | Default | Alternative |
|---|---|---|---|
| D13 | Overview home view (6 task cards + "Run all self-checks") | **Include** | Leave it out (4 required views + Backprop only) |
| D14 | Theme | **Follow Windows app mode**; screenshots in light | Always light |
| D15 | Backprop placement | **5th nav item** "Backprop (F)" | Sub-tab of Thermal |
| D16 | Demo format | **3–5 min video** recorded by you, script provided | Timestamped screenshot walkthrough |

### 0.3 Needed from you before or during the build
| Item | When needed |
|---|---|
| Approve VS 2022 Build Tools install (~4–6 GB) | Phase 0 |
| Enable Windows Sandbox (Windows feature, needs restart) | Phase 1 exit |
| Candidate ID (for `CandidateID_Workbench.zip`) | Phase 9 |
| Handwritten backprop appendix (≤ 2 pages, scanned) | Phase 7 |
| Demo recording | Phase 9 |
| Review + approve each phase before commit/push | Every phase |

---

## 1. Scoring strategy

Scoring anchors from the brief: 0 / 25 / 50 / 75 / 100 %. **100 % needs "complete, correct and supported by reproducible evidence"**, so every requirement maps to an evidence artefact (§15).

| Criterion (max) | 100 % evidence | Target |
|---|---|---|
| Interface (8) | Clear workflow, validation, progress/cancel, units everywhere, usable plots, on-screen verification badges, screenshots + demo | 7.5–8 |
| C++ backend & testing (12) | Portable core lib, typed errors, ~90 tests (unit/numerical/negative/integration), CI with sanitizers | 11.5–12 |
| Folder architecture (6) | Layout §4.3, CMake-enforced dependency rule, relative paths, vendored deps + `DEPENDENCIES.md` | 6 |
| Code readability (6) | clang-format/clang-tidy, small focused functions, contract comments | 5.5–6 |
| Version control (5) | Phase-by-phase commits + tags, `.gitignore/.gitattributes`, dependency records, CHANGELOG, bundle | 5 |
| Thermal FEA (14) = 6 + 5 + 3 | Correct formulation/assembly/BCs; exact-value tests + convergence evidence; plain-language learning write-up + **genuine** debugging log | 13.5–14 |
| CAD/ANSYS viz (10) | Both importers, legend/units, orbit/pan/zoom/fit, picking, provenance, negative fixtures, measured size limits | 9.5–10 |
| Multicore (10) = 4 + 4 + 2 | Real pool; reproducible bench (warm-up, 5 reps, median) + agreement; honest overhead/hybrid-CPU analysis | 9.5–10 |
| Embedded (8) = 4 + 4 | Datasheet-backed table, block diagram, JTAG explanation, safe integration plan; 3 working mocks + timeout recovery | 7.5–8 |
| Backprop (7) = 4 + 3 | **Your** handwritten derivation; C++ FD checks + 100-step loss plot + explanation | 6.5–7 |
| AI judgement (8) | Live AI-use log: prompts, iterations, verification, rejected suggestions (incl. the 12 design corrections), disclosure | 7–8 |
| Delivery/report (6) = 3 + 3 | Sandbox-tested Launch.exe + working VS Code config; ≤ 6-page report, reproducible evidence, limitations | 6 |
| **Total** | | **≈ 95–99** |

Gates: C++ backend ≥ 6/12, FEA ≥ 7/14, delivery ≥ 3/6, working backend, successful standalone launch. All are designed to be passed with margin.

---

## 2. Requirements

### 2.1 Functional (source: brief PDF)
| ID | Requirement |
|---|---|
| A1 | One coherent UI: Geometry, Thermal Analysis, Performance, Hardware views |
| A2 | File selection; parameter validation; progress/status; actionable errors; result export |
| A3 | Responsive UI during computation; cancel or safe completion without corrupting results |
| A4 | C++17 core separated from presentation/transport |
| A5 | Win11 x64: unzip → double-click `Launch.exe`; no terminal/admin/install/first-run build/internet |
| A6 | Lawfully bundled deps; VS Code one-action run config of the *packaged* app; extension prereqs documented; separate CMake build instructions |
| B1 | ASCII STL parsed in C++; triangles displayed; rotate/pan/zoom/fit-to-view |
| B2 | CSV `node_id,x_m,y_m,z_m,temperature_C`; coloured point cloud; legend + units; node inspection |
| B3 | Reject malformed/non-finite data; explain empty files; document tested size limits |
| B4 | One example each with provenance, units, schema notes, licence; synthetic CSV labelled synthetic |
| C1 | 1D steady FE, two-node linear elements; assemble stiffness + load; impose both Dirichlet BCs |
| C2 | N = 10, 20, 40, 80; T vs x plots; compare with T_exact |
| C3 | Max abs error at element midpoints via FE interpolation; convergence shown |
| C4 | Units on plots; CSV export; assumptions, units, BCs; one genuine debugging insight; learning explanation |
| D1 | Serial + CPU-parallel batch of independent solves (not just a UI thread) |
| D2 | Fixed-seed generator, positive varied k and q, configurable cases/mesh; identical cases/methods; deterministic order |
| D3 | p = 1, 2, ≤ 4 capped at logical CPUs; report CPU, cores, RAM, compiler, flags, workload, warm-up, 5 reps, median, speedup, efficiency |
| D4 | Compute-only timing; launch-to-ready separately; max serial/parallel diff ≤ 1e-8 °C; overhead and limitations explained; single-CPU disclosure |
| E1 | Labelled block diagram + interface/pin-voltage table (power, ground, voltage domains, level shifting, connectors, host comms, JTAG ownership, pinout assumptions) |
| E2 | Explain why the JTAG adapter is a debug interface, not a compute board; no shared connector/voltage/protocol assumption |
| E3 | C++ mocks: discovery, capabilities, connect/disconnect, one telemetry message, one recoverable failure; all 3 in UI; labelled simulated |
| E4 | Safe integration plan and limitations |
| F1 | Handwritten: forward pass, 4 chain-rule derivatives, numeric gradients, one simultaneous update, gradient-direction explanation, ≤ 2 pages |
| F2 | C++ without autodiff; central FD ε = 1e-6, abs error ≤ 1e-5 for all four; loss over 100 steps plotted and explained |
| S1 | `CandidateID_Workbench.zip` with Launch.exe, app, src, CMake, dependency/version info, editor config, data, tests, results, docs, README |
| S2 | Report ≤ 6 pages (excl. 2-page appendix); demo 3–5 min or timestamped screenshots with logs |
| S3 | Git bundle/repo + readable log; AI-use log (incl. ≥ 1 incorrect/unsuitable suggestion or a challenge test, no fabrication); ownership statement; recorded actual effort |
| S4 | Relative paths only; no credentials, personal data or absolute machine paths |

### 2.2 Non-functional
| NFR | Target |
|---|---|
| N1 Launch robustness | Starts on clean Win11 x64 with no GPU driver, no VC++ redist, no internet, from a read-only folder, from a path with spaces and Unicode |
| N2 Launch-to-ready | Measured from **Launch.exe start** → first presented frame; target < 1.5 s *(est.)* |
| N3 UI frame time | ≤ 16 ms during any job |
| N4 Cancel latency | ≤ 200 ms |
| N5 Numerics | FEA nodal error ≤ 1e-10; midpoint error = qh²/8k ± 1e-9; serial vs parallel bitwise equal (≤ 1e-8 required) |
| N6 Import scale | ≥ 1 M facets / ≥ 1 M nodes with progress; hard caps give clear messages |
| N7 Memory | < 1.5 GB peak at documented limits *(est.)* |
| N8 Determinism | Same inputs + seed → byte-identical CSVs (timing files excepted) on MSVC and GCC/clang |
| N9 Portability | Core builds on MSVC and Linux GCC/clang |
| N10 Package size | < 15 MB zipped, excluding the git bundle *(est.)* |

*(est.)* = design estimate, replaced by measured values in the report.

### 2.3 Constraints & assumptions
- Solo, 15 days, 40–50 h; **actual effort logged** in `docs/effort_log.md`.
- Free tools; open-licensed or synthetic data only.
- Packaging scripts contain no backend logic.
- You must be able to explain, rerun and modify anything live, so simplicity wins over cleverness.
- Evaluator: Win11 x64 laptop or VM, ≥ 4 logical CPUs, ≥ 8 GB RAM; VS Code available only for the editor-config check.
- Unsigned executables: the SmartScreen "More info → Run anyway" step is documented as a known limitation.

---

## 3. Technology stack

| Layer | Choice | Licence |
|---|---|---|
| Compiler / build | MSVC v143, CMake ≥ 3.25, Ninja, `CMakePresets.json` | — |
| UI | Dear ImGui (docking branch, pinned SHA), ImPlot (pinned) | MIT |
| Graphics | Direct3D 11 + DXGI (OS components) | OS |
| Shaders | HLSL compiled with `fxc` at build time → bytecode headers | — |
| Fonts | Inter, JetBrains Mono | SIL OFL 1.1 |
| Colormap | inferno (matplotlib, van der Walt & Smith) | CC0 |
| Tests | GoogleTest (pinned) | BSD-3 |
| CI | GitHub Actions: `windows-2022`, `ubuntu-24.04` (GCC 13 / clang with libstdc++ for floating-point `from_chars`) | — |

All third-party code is vendored in `third_party/` with `DEPENDENCIES.md` (name, version/SHA, URL, licence, what we use it for). Licence texts are in `LICENSES/` and also shipped in the ZIP.

---

## 4. Architecture

### 4.1 Component diagram
```text
┌──────────────────────────── CandidateID_Workbench/ (package root) ─────────────────────────┐
│ Launch.exe ─CreateProcess(--launch-t0=QPC)─► app/UniForge.exe        app/uniforge_cli.exe  │
│ (no logic)                       ┌──────────────────────────────────┐   (fea | bench |      │
│                                  │ PRESENTATION  src/app             │    backprop |         │
│                                  │  ui/: Theme, Fonts, Format,       │    gen-samples |      │
│                                  │       widgets/*                    │    sysinfo | all)     │
│                                  │  views/: Overview, Geometry,      │          │            │
│                                  │   Thermal, Performance, Hardware, │          │            │
│                                  │   Backprop, About                 │          │            │
│                                  │  render/: Camera, MeshRenderer,   │          │            │
│                                  │   PointCloudRenderer, Picking     │          │            │
│                                  │  platform/: Win32Window, D3D11Ctx,│          │            │
│                                  │   FileDialog, Paths, CrashHandler │          │            │
│                                  ├──────────────────────────────────┤          │            │
│                                  │ SERVICES  src/app/services        │          │            │
│                                  │  JobRunner · ResultStore ·        │          │            │
│                                  │  DeviceManager · ExportService ·  │          │            │
│                                  │  LogSink                          │          │            │
│                                  └───────────────┬──────────────────┘          │            │
│                                                  ▼                             ▼            │
│               ┌──────────── uniforge_core (static lib, portable C++17, std only) ─────────┐ │
│               │ common · thermal · io · perf · hw · ml                                    │ │
│               └───────────────────────────────────────────────────────────────────────────┘ │
│ regenerate_evidence.cmd (wrapper: app\uniforge_cli.exe all)   .vscode/  data/ results/ docs/│
└─────────────────────────────────────────────────────────────────────────────────────────────┘
```

### 4.2 Dependency rules (CMake-enforced + CI lint)
- `uniforge_app → services → uniforge_core`; `uniforge_cli → uniforge_core`; `tests → uniforge_core (+ services)`.
- `uniforge_core` links only the standard library. The only OS-specific code is `perf/SystemInfo_win.cpp` / `_linux.cpp` behind one interface.
- CI lint fails on: `#include` of imgui/windows headers inside `src/core` (except SystemInfo), literal colours (`ImVec4(`/`IM_COL32(`) outside `src/app/ui/Theme*.cpp`, absolute paths or secrets anywhere in the tree.

### 4.3 Repository layout
```text
UniForge/
├── CMakeLists.txt  CMakePresets.json  .clang-format  .clang-tidy  .editorconfig
├── .gitignore  .gitattributes  LICENSE  README.md  CHANGELOG.md  DEPENDENCIES.md
├── LICENSES/                       third-party licence texts (MIT, BSD-3, OFL, CC0)
├── cmake/                          CompilerOptions.cmake, Warnings.cmake, Shaders.cmake
├── third_party/                    imgui/  implot/  googletest/  fonts/
├── src/
│   ├── core/
│   │   ├── common/                 Result.hpp, Error.hpp, CancelToken.hpp, Progress.hpp,
│   │   │                           CsvWriter, SvgPlot, MetaJson, Rng (portable uniform)
│   │   ├── thermal/                RodProblem, Mesh1D, ElementMatrices, Assembler,
│   │   │                           DirichletBC, ThomasSolver, Analytical, ErrorAnalysis,
│   │   │                           ConvergenceStudy
│   │   ├── io/                     LineReader, StlParser, ThermalCsvParser, ImportLimits
│   │   ├── perf/                   BatchGenerator, WorkerPool, SerialRunner, ParallelRunner,
│   │   │                           Benchmark, SystemInfo(_win/_linux)
│   │   ├── hw/                     IDeviceAdapter, MockTransport, McuAdapter, FpgaAdapter,
│   │   │                           JtagAdapter, DeviceStateMachine, RetryPolicy
│   │   └── ml/                     TinyNet, GradientCheck, Trainer
│   ├── app/
│   │   ├── platform/  render/  services/  ui/ (Theme, Fonts, Format, widgets/)  views/
│   │   ├── shaders/                *.hlsl
│   │   └── main.cpp  app.manifest (per-monitor-v2 DPI, UTF-8 code page)  app.rc (version)
│   ├── cli/                        main.cpp + commands/
│   └── launcher/                   Launch.cpp  launcher.rc
├── tests/                          unit/ numerical/ negative/ integration/ fixtures/ golden/
├── data/                           geometry/  thermal/  PROVENANCE.md
├── results/                        csv/  plots/  benchmarks/  logs/
├── docs/
│   ├── architecture.md  thermal_fea.md  performance.md  embedded_design.md
│   ├── embedded_block_diagram.svg  test_checklist.md  walkthrough.md  demo_script.md
│   ├── ai_usage_log.md  effort_log.md  ownership.md
│   ├── design/                     the 3 source design docs + this spec
│   ├── report/                     report source + report.pdf
│   ├── appendix_handwritten/       your scans
│   └── demo/                       screenshots, video link/file
├── tools/                          package.ps1, hygiene_scan.ps1, make_bundle.ps1
├── .github/workflows/              ci.yml
└── .vscode/                        launch.json, tasks.json, extensions.json (repo versions)
```

---

## 5. Core module specification

### 5.1 Error model (used everywhere)
```cpp
enum class ErrorCode { InvalidInput, EmptyFile, ParseError, NonFinite, LimitExceeded,
                       UnsupportedFormat, IoError, Cancelled, DeviceNotFound, DeviceTimeout,
                       DeviceFault, Internal };
struct Error {
    ErrorCode code;
    std::string message;               // what happened + where
    std::string hint;                  // what the user should do
    std::optional<std::size_t> line;   // 1-based, for parsers
    bool retryable = false;
};
template <class T> class Result;       // holds T or Error; Result<void> specialisation
```
Rules: no exceptions cross the public core API. `std::bad_alloc` and unexpected exceptions are caught at the job boundary and turned into `Internal`. The UI only displays `message` + `hint`, never invents its own text.

### 5.2 Contracts
| Function | Pre-conditions (violations → `Error`, never UB) | Post-conditions |
|---|---|---|
| `Result<FeaSolution> solveRod(const RodProblem&, const CancelToken* = nullptr)` | L, A, k > 0 finite; q, T0, TL finite; 1 ≤ N ≤ 10⁷ | `x.size()==T.size()==N+1`; `T[0]==T0`, `T[N]==TL` exactly |
| `ErrorReport analyse(const RodProblem&, const FeaSolution&)` | same problem | midpoint arrays length N; nodal + midpoint max errors; energy residual |
| `Result<std::vector<StudyRow>> convergenceStudy(const RodProblem&, const std::vector<int>& Ns, ProgressFn, const CancelToken*)` | Ns non-empty, each ≥ 1 | ratio/order filled from row 2 on |
| `Result<StlMesh> parseAsciiStl(std::istream&, std::uintmax_t sizeHint, const ImportLimits&, ProgressFn, const CancelToken*)` | readable stream | ≥ 1 facet, finite coords, bbox; or typed error with line |
| `Result<ThermalNodeSet> parseThermalCsv(…same…)` | readable stream | ≥ 1 row, unique ids, finite, T ≥ −273.15 °C, Tmin/Tmax |
| `std::vector<RodProblem> generateBatch(std::uint64_t seed, int cases, int N)` | cases ≥ 1, N ≥ 1 | identical on all compilers: `mt19937_64` + own `[a,b)` mapping (**not** `std::uniform_real_distribution`, whose output is implementation-defined) |
| `Result<std::vector<CaseResult>> runSerial(cases, const CancelToken*)` | — | `out[i]` ↔ `cases[i]` |
| `Result<std::vector<CaseResult>> runParallel(cases, int workers, const CancelToken*)` | workers ≥ 1 | `out[i]` ↔ `cases[i]`; bitwise equal to serial |
| `Result<BenchmarkReport> benchmark(const BenchmarkConfig&, ProgressFn, const CancelToken*)` | valid cfg | worker clamp recorded; exactly 1 warm-up + 5 timed reps per p |
| `IDeviceAdapter::connect()` | Disconnected | Connected, or `Error{DeviceTimeout, retryable=true}` |
| `Forward forward(Params, x)`, `Grads backward(Params, x, y)`, `Params sgdStep(Params, Grads, lr)`, `GradCheck centralDiff(Params, x, y, eps)`, `std::vector<double> train(Params, x, y, lr, steps)` | finite | analytic gradients, no autodiff; `train` returns steps+1 losses |

### 5.3 Data models
```cpp
struct RodProblem    { double L=1, A=1e-4, k=10, q=1e4, T0=100, TL=20; int elements=10;
                       Result<void> validate() const; };
struct FeaSolution   { std::vector<double> x, T; double heatOutLeft_W, heatOutRight_W; };
struct ErrorReport   { double maxNodalErr, maxMidErr; std::vector<double> midX, midFe, midExact; double energyResidual_W; };
struct StudyRow      { int N; double h, maxMidErr, expected, ratioToPrev, order, maxNodalErr, energyResidual_W; };

struct Vec3d         { double x, y, z; };
struct ImportStats   { std::uintmax_t bytes; std::size_t lines; double parseSeconds; std::vector<std::string> warnings; };
struct StlMesh       { std::string name; std::vector<std::array<Vec3d,3>> tri; std::vector<Vec3d> normal;
                       Vec3d bbMin, bbMax; std::size_t degenerate; ImportStats stats; };
struct ThermalNode   { std::int64_t id; Vec3d p; double T_C; };
struct ThermalNodeSet{ std::vector<ThermalNode> nodes; double Tmin, Tmax; Vec3d bbMin, bbMax;
                       bool syntheticFlag; ImportStats stats; };
struct ImportLimits  { std::uintmax_t maxBytes = 512ull<<20; std::size_t maxFacets = 5'000'000, maxNodes = 5'000'000; }; // tuned to measurements

struct CaseResult    { std::vector<double> T; double Tmax, xAtTmax; };
struct BenchRow      { int workers; std::array<double,5> repsSec; double median, min, max, speedup, efficiency; };
struct SystemInfo    { std::string cpu, os, compiler, flags; int physicalCores, logicalCpus; std::uint64_t ramBytes; };
struct BenchmarkConfig { std::uint64_t seed = 20261101; int cases = 4000, elements = 4000; std::vector<int> workers{1,2,4}; };
struct BenchmarkReport { SystemInfo sys; BenchmarkConfig cfg; std::vector<int> effectiveWorkers;
                         std::vector<BenchRow> rows; double maxAbsDiff_C; bool agreementPass; std::vector<std::string> notes; };

enum class DeviceKind  { Microcontroller, Fpga, JtagProbe };
enum class DeviceState { Disconnected, Connecting, Connected, Faulted, Recovering };
struct DeviceInfo    { std::string id, model, transport; DeviceKind kind; bool simulated = true; };
struct Capability    { std::string name, description; };
struct Telemetry     { std::chrono::system_clock::time_point t; std::string channel; double value; std::string unit; };

struct Params { double w, b, v, c; };  struct Grads { double dw, db, dv, dc; };
struct Forward { double z, h, yhat, err, loss; };
struct GradCheck { Grads analytic, numeric, absDiff; bool pass; };
```
UI-only: `RenderMesh` holds float positions/normals **recentred on the bbox centre before the float conversion** to avoid precision loss.

---

## 6. Concurrency model
```text
UI thread ── Win32 msg loop + ImGui + D3D11 (ONLY thread touching D3D/ImGui)
  │ submit(Job) (rejected with "busy" if a job is running; Run buttons disabled meanwhile)
  ▼                                           ▲ per-frame poll: progress, completion
JobRunner thread ── one job at a time ── may own a WorkerPool (p threads, joined before return)
DeviceManager thread ── 10 Hz tick; owns adapters; UI → commands via mutex-guarded queue;
                        telemetry → ring buffers (mutex) → UI snapshot per frame
```
- **Progress:** `std::atomic<double>` + mutex-guarded stage text + elapsed time.
- **Cancellation:** `CancelToken` (atomic bool) checked per line-batch (parsers), per case (runners), per mesh (study), per rep (bench).
- **Atomic publish:** `ResultStore` holds `std::shared_ptr<const T>` per domain, swapped **only on success**. Cancel or error keeps the previous result (test: `jobs.cancel_keeps_previous_result`).
- **Atomic export:** write `name.tmp`, then rename.
- **WorkerPool:** fixed `p` threads; chunked indices from `std::atomic<size_t>` (chunk = max(1, cases/(8p))); each worker writes only its own `out[i]`; no locks in the hot path; worker exceptions captured as `std::exception_ptr`, rethrown as `Error{Internal}` after join.
- **Why one job at a time:** simpler state, and no interference with benchmark timings.

---

## 7. Task specifications

### 7.1 Task A: shell, delivery, launch
- Views: **Overview** (D13), Geometry (B), Thermal Analysis (C), Performance (D), Hardware (E), Backprop (F, D15), About/Diagnostics.
- Global: StatusBar, LogPanel (filter, search, export), RunBar pattern, validation per §8.
- `Launch.exe`: Win32, no console; resolves its own directory; starts `app\UniForge.exe --launch-t0=<QPC>` with that directory as working dir; on failure shows a message box with the path tried, the system error text and the hint "Re-extract the ZIP and keep the folder structure"; exits non-zero.
- **Launch-to-ready:** two numbers, Launch.exe start → first `Present` (headline) and UniForge.exe start → first `Present`; shown in the status bar and About, logged to `logs/launch.log`.
- Read-only package dir: startup write-probe; logs and `imgui.ini` fall back to `%LOCALAPPDATA%\UniForge\`; exports always go through a Save dialog (default `Documents\UniForge\exports\`).
- Wide-char Win32 APIs + `std::filesystem::path` everywhere (spaces/Unicode paths).
- `CrashHandler`: `SetUnhandledExceptionFilter` → `logs/crash_<time>.txt` + minidump via system `dbghelp`, then a message box.
- D3D11: try hardware, else WARP (status-bar warning "Software rendering"); handle `DXGI_ERROR_DEVICE_REMOVED` by recreating device + buffers.

### 7.2 Task B: STL & ANSYS-CSV visualisation
**Parsers** (streamed 64 KB line reader, progress = bytes/size, `std::from_chars`, locale-independent):
- STL: state machine `solid → facet normal → outer loop → vertex×3 → endloop → endfacet … endsolid`. Rejects: empty file (0 bytes) vs `solid` with 0 facets (different messages), wrong vertex count, unknown keyword, non-finite/overflow numbers, truncated file (EOF inside a facet), binary STL (sniff: missing `solid` or size == 84 + 50·n) → `UnsupportedFormat` with "Export as ASCII STL". Zero normals recomputed; degenerate triangles counted as warnings.
- CSV: strips BOM; accepts CRLF/LF; header must be exactly `node_id,x_m,y_m,z_m,temperature_C` (surrounding spaces trimmed); per-row errors with line numbers for missing/extra columns, non-numeric, non-finite, duplicate id, T < −273.15 °C, quoted fields; header-only file → "No data rows".
- Limits enforced with the measured limit in the hint. **Measured** parse time and peak memory at 10 k / 100 k / 1 M facets and nodes → `results/benchmarks/import_limits.md` + PROVENANCE.md.

**Viewer:** offscreen D3D11 render target in `ImGui::Image`; MSAA 4× if available; flat-shaded mesh + optional wireframe; instanced camera-facing point quads with inferno 1D texture; orbit (LMB), pan (MMB / Shift+LMB), zoom to cursor (wheel), Fit (`F`, bbox sphere into FOV), Reset (`R`), axes triad, bbox dimensions in m. Picking: CPU screen-space nearest within 6 px → NodeInspector (id, x/y/z [m], T [°C]) + highlight ring.

**Sample data** (generated by `uniforge_cli gen-samples`, committed):
- `data/geometry/rod_10x10mm_1m.stl`: self-created 1 m × 10 mm × 10 mm bar (A = 1e-4 m², the Task C rod).
- `data/thermal/rod_nodal_temperature_SYNTHETIC.csv`: nodes on that bar, T from the analytical solution plus a documented small lateral variation. **"SYNTHETIC" in the filename, PROVENANCE.md and the UI chip. Not an ANSYS export.**
- Optional: `heatsink_fins.stl` (self-created) for a richer visual.
- `PROVENANCE.md`: origin, generator command + seed, units, schema, licence (repo licence), tested size limits.

### 7.3 Task C: 1D thermal FEA
- Weak form documented. Element: `Ke = (kA/h)·[[1,−1],[−1,1]]`, `fe = (qAh/2)·[1,1]`. Assembly into tridiagonal arrays `a, b, c, f`.
- Dirichlet by **symmetric elimination**: `f[1] −= a[1]·T0; a[1] = 0; b[0] = 1; c[0] = 0; f[0] = T0` (mirrored at node N).
- Thomas solve. Stability note: the reduced matrix is SPD and **irreducibly diagonally dominant** (strict in rows adjacent to Dirichlet nodes), so no pivoting is needed. Dense Gaussian elimination with partial pivoting is a test-only oracle.
- Post-processing: midpoint FE interpolation `½(T_i+T_{i+1})` vs `T_exact(x_mid)`; nodal error; end heat flows from the **unconstrained** equations (reactions); energy balance `heatOutLeft + heatOutRight = q·A·L` (= 1 W).
- Expected values: `T_exact = 100 + 420x − 500x²`, `T_max = 188.2 °C @ x = 0.42 m`; midpoint errors 1.25, 0.3125, 0.078125, 0.01953125 °C; ratio 4; order 2; nodal error ~machine precision (1D linear FE with consistent load is nodally exact here).
- Outputs: `results/csv/fea_N{10,20,40,80}_nodes.csv` (`x_m,T_fe_C,T_exact_C,abs_err_C`), `…_midpoints.csv`, `convergence.csv` (`N,h_m,max_mid_err_C,expected_qh2_8k_C,ratio,order,max_nodal_err_C,energy_residual_W`) + `.meta.json` sidecars; plots `T_vs_x.svg`, `error_vs_h_loglog.svg` (slope-2 reference).
- `docs/thermal_fea.md`: governing equation in plain words, assumptions (steady, 1D, constant k, insulated lateral surface, uniform q), units, BCs, weak form → matrices, why the profile is parabolic and peaks at 0.42 m, why nodal exactness happens, why the error is O(h²), and a **debugging log written as bugs actually happen** (no invented stories).

### 7.4 Task D: multicore
- Batch: `Rng(seed)` portable; k ~ U[5, 50] W/(m·K), q ~ U[1e3, 5e4] W/m³; L, A, T0, TL fixed at brief values; configurable cases and N.
- Same `solveRod` in serial and parallel; result buffers **preallocated outside the timed region**; `steady_clock` around compute only.
- Protocol: p ∈ {1, 2, 4} ∩ [1, logical]; per p: 1 warm-up + 5 timed reps → median/min/max; speedup `T1/Tp`, efficiency `speedup/p`; agreement `max |T_serial − T_parallel|` over all nodes of all cases (expect 0.0, require ≤ 1e-8).
- Defaults: report preset 4 000 cases × 4 000 elements (≈ 128 MB per result set *(est.)*), tuned so T1 ≥ 1 s and recorded; quick preset 500 × 1 000 for the UI.
- Report files: `results/benchmarks/bench_<date>.{json,csv,md}` with CPU, physical/logical, RAM, OS, compiler + version, flags (`/O2 /MT /DNDEBUG …`), workload, seed, warm-up, reps.
- `docs/performance.md`: results table + plots; overhead (thread start, chunk scheduling, allocation), memory behaviour (per-solve working set ≈ 160 KB at N = 4 000, fits L2), **Intel hybrid P/E cores and turbo effects on the i5-14600K**, Amdahl. Optional p = 8/16 series labelled "beyond requirement". If logical = 1: forced p = 2 demonstration + automatic "speedup not assessable" note.

### 7.5 Task E: embedded integration
**Design doc (`docs/embedded_design.md` + SVG block diagram):** all electrical values taken from official datasheets/user manuals in Phase 6, each with a cited document and section. Nothing is filled in from memory.
- Block diagram: host PC → powered USB hub → (MCU board via ST-LINK USB: VCP data + SWD debug), (FPGA board via USB: FTDI UART data + on-board JTAG), (J-Link via USB → 10/20-pin debug header of a chosen target); power domains, ground reference, data vs debug paths colour-coded and labelled.
- Interface/pin-voltage table: per device: supply source and range, I/O voltage, ground, connectors, host protocol, debug signals (TCK/TMS/TDI/TDO/nRESET/VTref or SWDIO/SWCLK), level shifting needed? where?, pinout assumptions.
- JTAG explanation: a probe drives another device's TAP for debug and boundary scan; it has no application compute or telemetry of its own; signals belong to the target's debug port; VTref senses the target's I/O voltage.
- Signal-ownership conflict example: on-board ST-LINK vs external probe (jumper settings) on the Nucleo.
- **Safe integration plan:** common ground first, check VTref before driving, power sequencing, no back-powering through USB, current limits, ESD handling, bring-up order (one device at a time), and limitations.

**Mocks (`core/hw`):** `MockTransport(seed, latencyMs, jitterMs, failMode ∈ {None, TimeoutOnce, TimeoutAlways, Disconnect})`. Discovery returns 3 devices with simulated IDs. Capabilities: MCU (UART telemetry, GPIO), FPGA (XADC temperature, DONE status), JTAG (IDCODE scan, VTref sense). State machine `Disconnected → Connecting → Connected → Faulted → Recovering → Connected`. RetryPolicy: timeout 250 ms, up to 3 retries with 100/200/400 ms backoff; success logs "recovered after n retries"; exhausted → `Faulted` with a hint. Telemetry: MCU `vdd_V`, `die_temp_C`; FPGA `xadc_temp_C`, `done`; JTAG `vtref_V`, `idcode` (all labelled simulated). `DeviceInfo.simulated == true` is asserted in tests.

### 7.6 Task F: backprop
- C++ pure functions in `core/ml`: forward (z = wx + b, h = tanh z, ŷ = vh + c, J = ½(ŷ − y)²); backward `dc = e`, `dv = e·h`, `db = e·v·(1 − h²)`, `dw = db·x`; simultaneous update from original params; central FD per parameter with ε = 1e-6, pass if |diff| ≤ 1e-5; train 100 further steps at lr = 0.1 → 101 losses.
- Expected: h = 0.462117, ŷ = 0.462117, e = −1.537883, J = 1.182542; dw = db = −1.209464, dv = −0.710682, dc = −1.537883; updated w = 0.620946, b = 0.120946, v = 1.071068, c = 0.153788.
- Outputs: `results/csv/backprop_gradients.csv`, `loss_curve.csv`, `loss_curve.svg`; explanation of the curve (one sample, four parameters → loss heads to 0; behaviour and rate at lr = 0.1; tanh saturation) written from the *measured* curve.
- Handwritten appendix (you): checklist in `docs/appendix_handwritten/CHECKLIST.md`.

---

## 8. UI specification

### 8.1 Principles
1. Evidence on screen: each view shows its own verification badges with tolerances.
2. Units everywhere, SI, label format `name [unit]`.
3. One way to do each thing: same RunBar, ExportMenu and error presentation everywhere.
4. Status is never colour-only: glyph + word + colour.
5. Calm UI: no animation except progress; no disappearing toasts; status bar + log are the feedback channel.
6. Simulated/synthetic is always visibly marked.

### 8.2 Tokens (`src/app/ui/Theme.*`; views never use literal colours/sizes)
| Token | Light | Dark |
|---|---|---|
| `bg.app` / `bg.surface` / `bg.surface-alt` | `#F5F7FA` / `#FFFFFF` / `#EEF1F5` | `#111317` / `#181B21` / `#20242C` |
| `border.default` / `border.focus` | `#D3D9E1` / `#2563EB` | `#323844` / `#5B95FF` |
| `text.primary` / `.secondary` / `.disabled` | `#1A1E24` / `#59616E` / `#9AA1AC` | `#E7EAF0` / `#9AA3B2` / `#5E6573` |
| `accent` / `.hover` / `.active` | `#2563EB` / `#1D4ED8` / `#1E40AF` | `#5B95FF` / `#7AA9FF` / `#3D7BF0` |
| `status.success` ✓ | `#15803D` | `#3FB950` |
| `status.warning` ! | `#B45309` | `#D29922` |
| `status.error` ✕ | `#B91C1C` | `#F85149` |
| `status.info` i | `#0E7490` | `#39C5CF` |
| `status.simulated` ◇ | `#7C3AED` | `#A78BFA` |

Contrast targets (text ≥ 4.5:1, non-text ≥ 3:1) are **verified with a checker in Phase 8**; values may be adjusted.

| Data-viz token | Value |
|---|---|
| `viz.series` N = 10/20/40/80 | `#D55E00`, `#0072B2`, `#009E73`, `#CC79A7` (Okabe–Ito), distinct markers |
| `viz.series` extras | `#E69F00`, `#56B4E9` |
| `viz.reference` | `text.primary`, dashed 1.5 px, drawn on top (exact solution, slope-2 guide, ideal speedup) |
| `viz.colormap` | inferno (CC0) |
| `viz.mesh.fill` | `#9FB3C8` / `#5D6B7A` |
| `viz.pick` | `#00B3FF` 2 px ring |

| Type token | Font, size @100 % DPI |
|---|---|
| `type.title` / `type.heading` | Inter SemiBold 20 / 16 px |
| `type.body` / `type.label` | Inter Regular 15 / 13 px |
| `type.mono` | JetBrains Mono 14 px (tables, log, IDs; numerics right-aligned) |

Spacing 4/8/12/16/24/32 px; control height 30 px; card padding 16 px; radius 4 (controls) / 6 (cards); no shadows; motion = progress only.

Glyph ranges: Basic Latin, Latin-1 (°, ², ³, ·), Greek (ε, Δ), plus `−`, `≤`, `→`, `✓`, `✕`, `◇`. Atlas rebuilt on DPI change.

### 8.3 Number formatting (`ui/Format.hpp`, unit-tested)
| Quantity | Format | Example |
|---|---|---|
| Temperature | 4 dp | `188.2000 °C` |
| FE errors | 6 significant | `1.250000 °C`, `1.953125e-02 °C` |
| Ratio / order | 4 dp | `4.0000` |
| Time | 3 significant, auto unit | `412 ms` |
| Speedup / efficiency | 2 dp / percent | `3.71×`, `92.8 %` |
| Agreement | scientific | `0.00e+00 °C (≤ 1e-08 ✓)` |
| Coordinates | 6 significant | `0.420000 m` |
| CSV exports | `%.17g`, ASCII, header `name_unit` | `temperature_C` |

### 8.4 Components (`src/app/ui/widgets/`)
AppShell · NavRail (task tags + status dots; compact < 1100 px) · ViewHeader · VerificationBadge (pass/fail/pending/n.a., tooltip with numbers + tolerance) · ParamField (label, unit, range, default, ↺ reset, invalid state disables Run with a reason) · FileField (browse, drag-drop, recent) · RunBar (Run ↔ Cancel, progress, stage, elapsed; `Ctrl+Enter` / `Esc`) · StatusBar · LogPanel (`Ctrl+L`) · InlineAlert (info/warning/error/simulated) · DataTable (sortable, mono numerics, pass column, `Ctrl+C` TSV) · PlotPanel (units on axes, linear/log–log, `Ctrl+E` PNG) · Viewport3D (Fit/Reset/Wireframe/Point size/Axes; `F`/`R`/`W`) · ColorLegend · NodeInspector · DeviceCard · SimulatedBadge · ExportMenu · EmptyState.

Rules: keep the last good result visible (dimmed, "updating…") while a job runs; same colour for the same N in every plot; tolerance next to every pass/fail; disabled controls always explain why.

### 8.5 Patterns
- **Form → Run → Result** (Thermal, Performance, Backprop): params card → RunBar → plots + table → badges → ExportMenu.
- **Import** (Geometry): FileField / drop → progress with bytes → success summary ("Loaded 12 facets · 0 degenerate · bbox 1.000 × 0.010 × 0.010 m · 3 ms") + auto-fit; failure alert; previous model kept.
- **Error copy:** `<what happened> <where> — <how to fix>`, e.g. "Line 214: expected `vertex x y z` but found `vertx 0 0 1`. The file may be truncated — re-export it as ASCII STL." Codes go to the log only.
- **Long job:** stage + elapsed + Cancel; "Cancelled — previous results kept"; "Done in 412 ms".
- **Export:** Save dialog, temp → rename, "Saved convergence.csv (4 rows) · Open folder".
- **Simulated/synthetic:** Hardware banner + per-card badges; "SYNTHETIC DATA" chip on synthetic CSV.
- **First run:** opens on Overview with one-click sample loading.

### 8.6 View blueprints
```text
Shell
┌──────────────┬──────────────────────────────────────────────────────────────────────┐
│ UniForge     │ Title · [Task C] · purpose                       [✓ badge][✓ badge] │
│ ○ Overview   ├──────────────────────────────────────────────────────────────────────┤
│ ● Geometry  B│                           view content                               │
│ ● Thermal   C│                                                                      │
│ ● Perf.     D├──────────────────────────────────────────────────────────────────────┤
│ ● Hardware  E│ Log ▾ 10:22:04.512 INFO job 7 thermal.study done 12.4 ms             │
│ ● Backprop  F│                                                                      │
│ ⚙ About      │                                                                      │
├──────────────┴──────────────────────────────────────────────────────────────────────┤
│ ● Idle · Saved convergence.csv · D3D11 HW · 20 CPUs · Ready in 0.8 s                  │
└─────────────────────────────────────────────────────────────────────────────────────┘
```
- **Overview:** six task cards A–F with live badges + "Open view →"; "Run all self-checks" (FEA study + backprop checks + quick bench ≈ 1 s). UI over existing core calls only.
- **Geometry:** left: STL/CSV FileFields, SYNTHETIC chip, show-mesh/points toggles, point size, NodeInspector; right: Viewport3D with legend at the right edge, bbox dims bottom-right. Badges: STL valid, CSV valid, non-finite rejected.
- **Thermal:** left: params (brief defaults, ↺ reset), mesh checkboxes 10/20/40/80, RunBar; right tabs: T vs x (+ dashed exact), Error vs h (log–log + slope-2), Midpoint errors; bottom: convergence table + ExportMenu. Badges: midpoint error = qh²/8k, order ≈ 2, energy balance, T_max @ 0.42 m.
- **Performance:** left: seed, cases, elements, workers (cap note), presets Quick/Report, RunBar ("Rep 3/5 · p = 2"); right tabs: Timing (median bars + min–max whiskers + ideal line), Speedup & efficiency, System; table p/reps/median/speedup/efficiency. Badges: agreement, order preserved, 5 reps + warm-up. Single-CPU warning alert when relevant.
- **Hardware:** simulated banner; Discover; 3 DeviceCards (state chip, capabilities, telemetry sparkline, Connect/Disconnect, Inject timeout, Retry/Reset fault, retry trail); "Design notes →" opens `docs/embedded_design.md`.
- **Backprop:** cards Forward pass / Gradients (analytic, FD, |diff|, ✓) / After one update; loss plot (log-y toggle) + explanation. Badges: 4/4 FD checks, update matches brief.
- **About/Diagnostics:** version + git hash, build date, compiler + flags, render backend, both launch-to-ready numbers, paths, licences, AI-assistance disclosure line, Open logs folder.
- Minimum window 1024 × 680; layout from tokens (not saved docking state), so a fresh machine always looks right.

### 8.7 Accessibility
Keyboard navigation + 2 px focus ring + F1 shortcut overlay; status never colour-only; per-monitor-v2 DPI, tested at 100/150/200 %. **Disclosed limitation:** no screen-reader support (ImGui exposes no UI Automation tree).

---

## 9. Storage, logging, diagnostics
| Data | Location | Format |
|---|---|---|
| Inputs | user files; samples in `data/` | ASCII STL, CSV |
| Evidence | `results/` | clean CSV + `.meta.json` sidecar (tool version, git hash, params, seed, units, UTC time); SVG plots; bench JSON/CSV/MD |
| Logs | `<pkg>/logs/` or `%LOCALAPPDATA%\UniForge\logs\` | `2026-11-03T10:22:04.512Z INFO job=7 thermal.study done 12.4 ms N=[10,20,40,80]` |
| UI layout | `%LOCALAPPDATA%\UniForge\imgui.ini` | ImGui ini |
| Crash | `logs/crash_<time>.txt` + `.dmp` | text + minidump |

Metrics logged automatically: both launch-to-ready numbers, per-job duration, import MB/s, render backend, bench summary. Demo timestamps come from these logs.

---

## 10. Testing & CI

| Label | Content | ≈ count |
|---|---|---|
| `unit` | element matrices, assembler, CSV/SVG/meta writers, Rng portability vectors, RetryPolicy, DeviceStateMachine, Format rules | 28 |
| `numerical` | nodal exactness, midpoint = qh²/8k (1e-9), ratio 4, order 2, energy balance, Thomas = dense oracle, T_max @ 0.42, backprop values/FD/update/loss monotone, serial == parallel bitwise | 22 |
| `negative` | 15 STL + 12 CSV fixtures (each asserts code + line), invalid params, limits, cancellation | 35 |
| `integration` | CLI `all` regenerates CSVs = golden files; JobRunner cancel keeps previous result; export atomicity; bench smoke; import → render-model pipeline (UI-less) | 8 |
| manual | `docs/test_checklist.md`: orbit/pan/zoom/fit, picking, DPI 100/150/200, light/dark, keyboard-only demo, Sandbox launch | — |

```text
push ─► ci.yml
        ├─ windows-msvc : preset release → /W4 /WX build → ctest → package.ps1 → upload ZIP artefact + JUnit XML
        ├─ linux-asan   : core + tests, GCC 13, -fsanitize=address,undefined
        ├─ linux-tsan   : perf + services tests, -fsanitize=thread
        └─ lint         : clang-format check, dependency-rule grep, literal-colour grep, hygiene scan
```

---

## 11. Packaging & delivery
`tools/package.ps1` (packaging only): configure → build Release → ctest → stage → hygiene scan → zip.
```text
CandidateID_Workbench/
├── Launch.exe
├── regenerate_evidence.cmd           (calls app\uniforge_cli.exe all; no logic)
├── app/UniForge.exe  app/uniforge_cli.exe   (static CRT; only OS DLLs, verified via dumpbin /dependents)
├── .vscode/launch.json               "Run UniForge (packaged)" → ${workspaceFolder}/Launch.exe
├── .vscode/tasks.json                same, with no extension needed
├── src/ tests/ cmake/ third_party/ tools/ data/ results/ docs/ LICENSES/
├── CMakeLists.txt  CMakePresets.json  README.md  LICENSE  DEPENDENCIES.md  CHANGELOG.md
└── git/UniForge.bundle  git/git_log.txt
```
- The **repo** `.vscode/launch.json` additionally has "Run packaged (dist)" → `${workspaceFolder}/dist/CandidateID_Workbench/Launch.exe`. `package.ps1` writes the ZIP version, so the evaluator's one-action config always targets the packaged app.
- Extension prerequisite documented: *C/C++ (ms-vscode.cpptools)* for `launch.json`; `tasks.json` needs none.
- README: prerequisites, one-click steps, architecture, supported formats, test/build steps (separate CMake section), SmartScreen note, known limitations.
- **Clean-machine test (Phase 1 and Phase 9):** Windows Sandbox → copy ZIP → extract → double-click Launch.exe → walk all views → screenshots + logs. Also from a path with spaces/Unicode and a read-only folder.

---

## 12. Report & evidence package
- `docs/report/report.pdf` ≤ 6 pages: design choices, FEA verification, performance results, embedded design, test outcomes, limitations, effort summary.
- `docs/ai_usage_log.md`: kept **live every phase**: tool/model (Claude Code, Claude Opus 5.5), representative prompts, outputs, your edits, verification evidence, rejected/incorrect suggestions (starts with the 12 design-review corrections), deliberate challenge test.
- `docs/ownership.md`: original vs reused work (vendored libs, fonts, colormap, AI assistance).
- `docs/effort_log.md`: actual hours per day and phase.
- `docs/walkthrough.md`: "where is X / how do I change Y / how do I rerun test Z" to prepare for the verification discussion.
- `docs/demo_script.md`: 3–5 min script driven from the Overview view.

---

## 13. Phase plan with commits

**Process per phase:** I implement → build + tests green → you review the diff → I commit (your identity, no trailers) → you approve push → push + annotated tag. Verification before each push: `git log -n <k> --format='%an <%ae>%n%b'` shows only your name and no trailer lines.

| Phase | Commits (planned) | Exit criteria | Est. h |
|---|---|---|---|
| **0 Setup** | `phase0(repo): add readme skeleton, licence and git attributes` · `phase0(build): add cmake skeleton, presets and compiler options` · `phase0(docs): add design documents, ai-use log and effort log` | toolchain installed; `cmake --preset` configures | 2 |
| **1 Skeleton + packaging** | `phase1(deps): vendor imgui, implot, googletest and fonts` · `phase1(platform): win32 window and d3d11 device with warp fallback` · `phase1(ui): theme tokens, fonts and application shell` · `phase1(services): job runner with progress, cancellation and atomic publish` · `phase1(launcher): add launcher with launch timestamp and error dialog` · `phase1(tools): packaging script, vs code configs and hygiene scan` · `phase1(ci): windows build and test workflow` → tag `phase-1-skeleton` | **ZIP launches in Windows Sandbox**; A4/A5/A6 rows green | 6 |
| **2 Thermal FEA** | `phase2(core): result, error, cancel token, csv and svg writers` · `phase2(thermal): mesh, element matrices and tridiagonal assembly` · `phase2(thermal): dirichlet elimination and thomas solver` · `phase2(thermal): analytical comparison, midpoint errors and energy balance` · `phase2(tests): numerical fea tests with dense oracle` · `phase2(cli): fea command exporting csv and plots` · `phase2(results): fea evidence and convergence plots` · `phase2(docs): thermal fea formulation and debugging log` → tag | C1–C3 green; evidence committed | 6 |
| **3 Importers** | `phase3(io): streamed line reader and import limits` · `phase3(io): ascii stl parser with validation` · `phase3(io): thermal csv parser with validation` · `phase3(tests): negative fixtures for stl and csv` · `phase3(cli): sample data generator` · `phase3(data): sample files and provenance` · `phase3(results): measured import size limits` → tag | B3/B4 green | 5 |
| **4 Visual views** | `phase4(render): camera, mesh and point-cloud renderers` · `phase4(render): node picking and colour legend` · `phase4(views): geometry view` · `phase4(views): thermal analysis view with plots and export` · `phase4(views): overview view and verification badges` → tag | A1–A3, B1/B2, C2/C4 UI green; screenshots | 7 |
| **5 Multicore** | `phase5(core): portable rng and batch generator` · `phase5(perf): bounded worker pool and runners` · `phase5(perf): benchmark harness and system info` · `phase5(tests): determinism, agreement and order tests` · `phase5(ci): linux sanitizer jobs` · `phase5(views): performance view` · `phase5(results): benchmark report` · `phase5(docs): performance analysis` → tag | D1–D4 green | 5 |
| **6 Embedded** | `phase6(docs): embedded design, pin table and block diagram` · `phase6(hw): device interface, mock transport and retry policy` · `phase6(hw): mcu, fpga and jtag mock adapters` · `phase6(tests): adapter and recovery tests` · `phase6(views): hardware view` → tag | E1–E4 green | 4 |
| **7 Backprop** | `phase7(ml): forward, backward and update` · `phase7(ml): gradient check and trainer` · `phase7(tests): backprop value tests` · `phase7(views): backprop view and loss plot` · `phase7(docs): handwritten appendix and explanation` → tag | F1–F2 green (appendix from you) | 2.5 |
| **8 Hardening** | `phase8(app): crash handler and read-only fallback` · `phase8(tests): integration and golden-file tests` · `phase8(ui): contrast, dpi and keyboard pass` · `phase8(results): large-file and edge-case runs` → tag | full suite green on CI; design QA checklist done | 3.5 |
| **9 Delivery** | `phase9(docs): readme, report and ownership statement` · `phase9(docs): finalise ai-use and effort logs` · `phase9(release): package, bundle and final evidence` → tag `v1.0.0` | S1–S4 green; final ZIP Sandbox-tested | 5 |
| | | **Total** | **≈ 46** |

Commit messages above are proposals; each gets a body explaining *why* and listing the tests run.

---

## 14. Things only you can do
1. Handwritten backprop appendix (Task F, 4 marks).
2. Approve the toolchain download; enable Windows Sandbox.
3. Candidate ID.
4. Record the demo.
5. Learn the code well enough for the verification discussion (`docs/walkthrough.md` + a dry run with me before submission).
6. Keep the AI-use log honest. Commits carry only your name, but the brief requires disclosure of AI assistance in the log and ownership statement.

---

## 15. Traceability (phase exit = rows green)
| ID | Component | Tests | Evidence |
|---|---|---|---|
| A1–A2 | app/views, ui/widgets | Format + widget-state unit tests; manual checklist | screenshots, demo |
| A3 | JobRunner, ResultStore, ExportService | `cancel_keeps_previous_result`, `export_atomic_rename` | log excerpt, demo |
| A4 | CMake targets, lint | lint job | CI log, architecture.md |
| A5–A6 | launcher, package.ps1, .vscode | Sandbox checklist, dumpbin check | Sandbox screenshots, launch.log |
| B1–B2 | io, render, Geometry view | parser positive tests | screenshots, samples |
| B3 | io + ImportLimits | 27 negative fixtures; size runs | `import_limits.md` |
| B4 | data/ | — | `PROVENANCE.md` |
| C1–C3 | thermal | ~12 numerical tests | `fea_*.csv`, `convergence.csv`, plots |
| C4 | docs, Thermal view | — | `thermal_fea.md` incl. debugging log |
| D1–D4 | perf, Performance view | determinism, agreement, order, cap, cancel; TSan | bench JSON/CSV/MD, `performance.md` |
| E1, E2, E4 | docs | — | `embedded_design.md`, block diagram SVG |
| E3 | hw, Hardware view | adapter + retry tests | demo, log "recovered after 1 retry" |
| F1 | you | — | appendix scans |
| F2 | ml, Backprop view | values, FD, update, loss tests | `loss_curve.csv/.svg` |
| S1–S4 | tools, docs | hygiene scan | ZIP, report, AI log, ownership, effort, bundle |

---

## 16. Risks
| Risk | Impact | Mitigation |
|---|---|---|
| Evaluator VM without GPU driver | Launch gate | D3D11 + WARP; Sandbox test |
| Missing VC++ runtime | Launch gate | `/MT`; `dumpbin /dependents` check in package.ps1 |
| Missing glyphs (`?` for °C) | Interface marks | Embedded fonts with explicit ranges; QA check |
| VS Code config path wrong in ZIP | Delivery marks | ZIP-specific launch.json written by package.ps1; tested from the extracted ZIP |
| SmartScreen/AV on unsigned exe | Launch friction | Version resources, no packer, README note |
| Timing noise on hybrid CPU | Weak perf analysis | Median of 5, warm-up, spread reported, P/E discussion |
| Over-polish | Missed evidence | Phase exit criteria; no features beyond the brief except Overview |
| Integrity concerns | Score revision | Synthetic labelled; real debugging log; honest AI log; no fabricated interactions |
| You can't explain code in the discussion | Score revision | walkthrough.md + dry run |

---

## 17. Merge record (what changed vs the source documents)
**Taken from SysDesign:** requirement IDs, NFRs, contracts, error taxonomy, concurrency model, storage/logging, traceability, CI pipeline, scaling estimates, portable RNG, CSV + `.meta.json`, crash handler.
**Taken from Plan:** rubric targets, phase schedule, sample-data idea (rod STL + synthetic CSV), user-only tasks, risks.
**Taken from UI:** all of §8 and corrections #1–#12, applied as:
1. ZIP-specific VS Code config → `Launch.exe` (§11)
2. Embedded Inter/JetBrains Mono with glyph ranges (§3, §8.2)
3. Launch-to-ready from Launch.exe via `--launch-t0` (§7.1)
4. OpenMP rationale = runtime DLL (§0.1 D5)
5. "Irreducibly diagonally dominant" wording (§7.3)
6. CLI at `app/uniforge_cli.exe` + `regenerate_evidence.cmd` (§4.1, §11)
7. Okabe–Ito palette with fixed N mapping (§8.2)
8. Theme follows Windows; light screenshots (§0.2 D14)
9. CI on ubuntu-24.04 / GCC 13 for `from_chars` (§3)
10. Accessibility limitation disclosed (§8.7)
11. inferno CC0 recorded (§3)
12. Design docs kept as AI-judgement evidence (§12, `docs/design/`)

**New in the merge:** explicit commit list per phase (§13), per-phase review/push process, `docs/walkthrough.md` and `demo_script.md`, `import_limits.md` artefact, packaging `dumpbin` check, design QA in Phase 8.
