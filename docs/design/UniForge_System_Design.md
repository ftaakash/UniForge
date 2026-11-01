# UniForge — System Design

**Produced by:** `engineering:system-design` skill (Requirements → High-level → Deep dive → Scale & reliability → Trade-offs)
**Purpose:** Design to compare against `UniForge_Analysis_and_Development_Plan.md`. **Design only, nothing is built yet.**
**Target:** ≥ 95/100 with every shortlist gate passed with margin.

All numbers marked *(est.)* are design estimates and get replaced by measurements once the code exists.

---

## 1. Requirements

### 1.1 Functional requirements (traceable IDs)
Every ID maps to a component, a test and an evidence artefact in §7. That mapping is how we reach the rubric's "100 % = supported by reproducible evidence" anchor.

| ID | Requirement (source: PDF) |
|---|---|
| **A1** | One coherent UI with Geometry, Thermal Analysis, Performance, Hardware views |
| A2 | File selection; parameter validation; progress/status; actionable errors; result export |
| A3 | UI responsive during computation; cancel or safe completion **without corrupting results** |
| A4 | C++17+ core separated from presentation/transport |
| A5 | Win11 x64: unzip → double-click `Launch.exe` works; no terminal/admin/install/first-run build/internet |
| A6 | Lawfully bundled runtime deps; VS Code/Antigravity one-action run config; extension prereqs documented; separate CMake build docs |
| **B1** | Parse ASCII STL in C++; show triangles; rotate/pan/zoom/fit-to-view |
| B2 | Parse CSV `node_id,x_m,y_m,z_m,temperature_C`; coloured 3D point cloud; legend + units; node inspection |
| B3 | Reject malformed/non-finite data; explain empty files; document tested size limits |
| B4 | One example of each with provenance, units, schema notes, licence; synthetic CSV labelled synthetic |
| **C1** | 1D steady FE, 2-node linear elements; assemble K and load; impose both Dirichlet BCs |
| C2 | N = 10/20/40/80; T vs x plots; compare with T_exact |
| C3 | Max abs error at element midpoints via FE interpolation; convergence shown |
| C4 | Plot units; export CSV; assumptions, units, BCs, one genuine debugging insight; learning explanation |
| **D1** | Serial + CPU-parallel batch of independent solves (bounded pool / std::thread / OpenMP), not just a UI thread |
| D2 | Fixed-seed generator, varied positive k and q, configurable cases and mesh; identical cases/methods; deterministic order |
| D3 | 1, 2, ≤4 workers capped at logical CPUs; CPU, cores, RAM, compiler, flags, workload, warm-up, 5 reps, median, speedup, efficiency |
| D4 | Timing excludes I/O/render/compile; launch-to-ready reported separately; serial/parallel max diff ≤ 1e-8 °C; overhead/limitations explained; single-CPU disclosure |
| **E1** | Block diagram + pin/voltage table: power, ground, voltage domains, level shifting, connectors, host comms, JTAG ownership, pinout assumptions |
| E2 | Explain why JTAG adapter ≠ compute board; no shared connector/voltage/protocol assumption |
| E3 | C++ mocks: discovery, capabilities, connect/disconnect, one telemetry message, one recoverable failure; all 3 in UI; labelled simulated |
| E4 | Safe integration plan + limitations |
| **F1** | Handwritten forward pass, 4 chain-rule derivatives, numeric gradients, one simultaneous update, direction explanation (≤ 2 pages) |
| F2 | C++ without autodiff; central FD ε = 1e-6, |err| ≤ 1e-5 for all 4; loss over 100 steps plotted and explained |
| **S1** | `CandidateID_Workbench.zip` with Launch.exe, app, src, CMake, dep/version info, editor config, data, tests, results, docs, README |
| S2 | Report ≤ 6 pages; demo 3–5 min or timestamped screenshots with logs |
| S3 | Git bundle/repo + readable log; AI-use log (incl. one wrong suggestion or challenge test); ownership statement; recorded effort |
| S4 | Relative paths; no credentials, personal data or absolute machine paths |

### 1.2 Non-functional requirements
| NFR | Target | Rationale |
|---|---|---|
| Launch reliability | Starts on clean Win11 x64 with **no GPU driver, no VC++ redist, no internet, read-only folder, path with spaces/Unicode** | Mandatory gate |
| Launch-to-ready | < 1.5 s cold *(est.)*, measured and logged | Reported separately (D4) |
| UI frame time | ≤ 16 ms during any job; input handled every frame | A3, interface marks |
| Cancel latency | ≤ 200 ms from click to job stopped | A3 |
| Numerical | FEA nodal error ≤ 1e-10; midpoint error = qh²/8k ± 1e-9; serial/parallel diff = 0 (≤ 1e-8 required) | C, D |
| Import scale | STL ≥ 1 M facets, CSV ≥ 1 M nodes parsed with progress; hard caps produce a clear message | B3 |
| Memory | < 1.5 GB peak at documented limits *(est.)* | Evaluator laptops |
| Portability of core | Builds on MSVC and clang/Linux | Lets CI run TSan/ASan |
| Determinism | Same inputs + seed → byte-identical CSV outputs (except timing files) | Reproducible evidence |
| Package size | < 15 MB zipped excluding git bundle *(est.)* | Fast download, simple |

### 1.3 Constraints
- Solo developer, 15 days, 40–50 h, **effort must be logged**.
- No C++ toolchain on the dev machine yet; it has to be installed (VS 2022 Build Tools).
- Free tools and openly licensed/synthetic data only; any licence must allow redistribution in the ZIP.
- Packaging scripts may not contain backend logic.
- The candidate must be able to explain, rerun and modify any part live → **simplicity beats cleverness**.

### 1.4 Assumptions
- Evaluator runs Win11 x64 on a typical laptop (≥ 4 logical CPUs, ≥ 8 GB RAM), possibly inside a VM.
- Evaluator has VS Code available for the editor-config check (not needed for standalone launch).
- Unsigned executables are acceptable (SmartScreen "More info → Run anyway" documented as a known limitation).

---

## 2. High-level design

### 2.1 Component diagram
```text
┌──────────────────────────── Package root (CandidateID_Workbench/) ─────────────────────────────┐
│  Launch.exe ──CreateProcess──►  app/UniForge.exe                         uniforge_cli.exe      │
│  (Win32, ~100 KB,               ┌─────────────────────────────────────┐  (headless evidence    │
│   no logic)                     │ PRESENTATION  (src/app)             │   generator: fea,      │
│                                 │  Shell: NavRail · StatusBar · Log   │   bench, backprop,     │
│                                 │  Views: Geometry│Thermal│Perf│HW│ML │   gen-samples,         │
│                                 │  Render: D3D11 Mesh/PointCloud,     │   sysinfo)             │
│                                 │          Camera, Picking, Colormap  │        │               │
│                                 │  Platform: Win32 window, DPI,       │        │               │
│                                 │          FileDialog, Paths          │        │               │
│                                 ├──────────────┬──────────────────────┤        │               │
│                                 │ APPLICATION SERVICES                │        │               │
│                                 │  JobRunner · ResultStore · DeviceMgr│        │               │
│                                 │  ExportService · LogSink            │        │               │
│                                 └──────────────┬──────────────────────┘        │               │
│                                                ▼ (plain C++ calls only)        ▼               │
│                 ┌──────────────────────── uniforge_core (static lib, portable C++17) ────────┐ │
│                 │ common  : Result/Error, CancelToken, Progress, CsvWriter, SvgPlot, Log API │ │
│                 │ thermal : RodProblem, Mesh1D, Assembler, Thomas, BCs, Analytic, Errors     │ │
│                 │ io      : StlParser, ThermalCsvParser, ImportLimits                        │ │
│                 │ perf    : BatchGenerator, WorkerPool, Serial/ParallelRunner, Benchmark,    │ │
│                 │           SystemInfo (OS-specific .cpp behind interface)                   │ │
│                 │ hw      : IDeviceAdapter, MockTransport, Mcu/Fpga/JtagAdapter, StateMachine│ │
│                 │ ml      : TinyNet, GradientCheck, Trainer                                  │ │
│                 └────────────────────────────────────────────────────────────────────────────┘ │
│  data/  results/  docs/  src/  tests/  third_party/  .vscode/  CMakeLists.txt  README.md        │
└────────────────────────────────────────────────────────────────────────────────────────────────┘
```
**Dependency rule (enforced by CMake target links):** `app → services → core`, `cli → core`, `tests → core`. `core` links nothing but the standard library. A CI check fails if `core` includes any `imgui`/`windows.h` header outside `perf/SystemInfo_win.cpp`.

### 2.2 Key data flows
```text
(1) Import:  FileDialog → JobRunner[parseAsciiStl] → Result<StlMesh> ──publish──► ResultStore
             ResultStore (UI thread, next frame) → RenderMeshBuilder (float, recentred) → D3D11 buffers

(2) FEA:     ThermalView form ─validate─► JobRunner[convergenceStudy{10,20,40,80}]
             → vector<StudyRow + FeaSolution> → ResultStore → ImPlot plots / table → ExportService (CSV/PNG)

(3) Bench:   PerfView form ─validate─► JobRunner[benchmark(cfg)]
             benchmark: generateBatch(seed) → warm-up → for p∈{1,2,4}: 5×(runParallel(p) timed)
                        + runSerial → agreement(maxAbsDiff) → BenchmarkReport → ResultStore → table/charts/export

(4) Hardware: DeviceMgr thread ─tick 10 Hz─► adapters.poll() → telemetry ring buffers (mutex)
             UI reads snapshot per frame; user actions → command queue → DeviceMgr thread

(5) Evidence: uniforge_cli all → results/csv, results/plots (SVG), results/benchmarks (json/csv/md)
             integration test diffs regenerated CSVs vs committed golden files
```

### 2.3 API contracts (in-process; there is no network API)
There is no server, so the "API" is the C++ boundary between `core` and its callers. Contracts:

| Function | Pre-conditions (validated → error, never UB) | Post-conditions |
|---|---|---|
| `Result<FeaSolution> solveRod(const RodProblem&, const CancelToken*)` | L, A, k > 0 and finite; q, T0, TL finite; 1 ≤ N ≤ 10⁷ | `x.size()==T.size()==N+1`, `T[0]==T0`, `T[N]==TL` exactly |
| `ErrorReport analyse(const RodProblem&, const FeaSolution&)` | solution from same problem | midpoint arrays length N |
| `Result<StlMesh> parseAsciiStl(std::istream&, ImportLimits, ProgressFn, const CancelToken*)` | stream readable | facets ≥ 1, all coords finite, bbox computed; or typed error with line number |
| `Result<ThermalNodeSet> parseThermalCsv(...)` | same | rows ≥ 1, unique ids, finite, T ≥ −273.15; min/max T computed |
| `std::vector<RodProblem> generateBatch(uint64_t seed, int cases, int N)` | cases ≥ 1, N ≥ 1 | deterministic across runs/platforms (uses `mt19937_64` + own uniform mapping, **not** `std::uniform_real_distribution`, whose output is implementation-defined) |
| `Result<std::vector<CaseResult>> runParallel(cases, int workers, const CancelToken*)` | workers ≥ 1 | `out[i]` corresponds to `cases[i]`; bitwise equal to `runSerial` |
| `BenchmarkReport benchmark(const BenchmarkConfig&, ProgressFn, const CancelToken*)` | valid cfg | workers clamp recorded; medians from exactly 5 timed reps |
| `IDeviceAdapter::connect()` | state Disconnected | Connected, or `Error{DeviceTimeout, retryable=true}` |
| `Grads backward(Params, double x, double y)` | finite inputs | analytic gradients, no autodiff |

Error type (single taxonomy used everywhere):
```cpp
enum class ErrorCode { InvalidInput, EmptyFile, ParseError, NonFinite, LimitExceeded,
                       UnsupportedFormat, IoError, Cancelled, DeviceNotFound, DeviceTimeout,
                       DeviceFault, Internal };
struct Error { ErrorCode code; std::string message; std::string hint;   // hint = what the user should do
               std::optional<std::size_t> line; bool retryable = false; };
```
UI rule: show `message` + `hint` in the status bar and log panel; never a raw exception string.

### 2.4 Storage choices
| Data | Where | Format | Why |
|---|---|---|---|
| Inputs | user-chosen files; samples in `data/` | ASCII STL, CSV | Required by brief |
| Results (evidence) | `results/` in repo + package | CSV (plain, header row only, no comment lines) + **sidecar `.meta.json`** (tool version, git hash, params, seed, units, timestamp) | CSV stays clean for Excel/pandas; metadata stays machine-readable |
| Plots | `results/plots/` | SVG from C++ `SvgPlot` (CLI), PNG export from UI | Deterministic + readable in report |
| Benchmarks | `results/benchmarks/` | JSON (full), CSV (table), MD (human summary) | One source, three views |
| Logs | `<exe>/logs/` if writable else `%LOCALAPPDATA%\UniForge\logs\` | text, one line per event | Works from read-only extraction |
| UI layout | `%LOCALAPPDATA%\UniForge\imgui.ini` | ImGui ini | Never write into package dir by default |
| In-memory results | `ResultStore`: `std::shared_ptr<const T>` per domain | immutable snapshots | Safe sharing UI ↔ jobs, atomic publish |

No database: nothing needs querying or concurrent writers, so files are the simpler choice.

---

## 3. Deep dive

### 3.1 Data models
```cpp
struct RodProblem   { double L=1, A=1e-4, k=10, q=1e4, T0=100, TL=20; int elements=10; };
struct FeaSolution  { std::vector<double> x, T; double qLeft_W, qRight_W; };        // end heat flows
struct StudyRow     { int N; double h, maxMidErr, expected, ratioToPrev, order, maxNodalErr, energyResidual_W; };

struct Vec3d { double x, y, z; };
struct StlMesh      { std::string name; std::vector<std::array<Vec3d,3>> tri; std::vector<Vec3d> normal;
                      Vec3d bbMin, bbMax; std::size_t degenerate; ImportStats stats; };
struct ThermalNode  { std::int64_t id; Vec3d p; double T_C; };
struct ThermalNodeSet { std::vector<ThermalNode> nodes; double Tmin, Tmax; Vec3d bbMin, bbMax; ImportStats stats; };
struct ImportStats  { std::uintmax_t bytes; std::size_t lines; double parseSeconds; std::vector<std::string> warnings; };

struct CaseResult   { std::vector<double> T; double Tmax, xAtTmax; };
struct BenchRow     { int workers; std::array<double,5> repsSec; double median, min, max, speedup, efficiency; };
struct BenchmarkReport { SystemInfo sys; BenchmarkConfig cfg; std::vector<BenchRow> rows;
                         double maxAbsDiff_C; bool agreementPass; std::string notes; };
struct SystemInfo   { std::string cpu, compiler, flags, os; int physical, logical; std::uint64_t ramBytes; };

enum class DeviceKind { Microcontroller, Fpga, JtagProbe };
enum class DeviceState { Disconnected, Connecting, Connected, Faulted, Recovering };
struct DeviceInfo   { std::string id, model, transport; DeviceKind kind; bool simulated = true; };
struct Telemetry    { std::chrono::system_clock::time_point t; std::string channel; double value; std::string unit; };
```
**Render model (UI only):** `RenderMesh { vector<float> pos/normal (recentred on bbox centre); D3D buffers }`. Recentring before the float conversion avoids precision loss for files with large absolute coordinates.

### 3.2 Concurrency design (the core of A3 + D1)
```text
 UI thread (Win32 msg loop + ImGui + D3D11; ONLY thread touching D3D/ImGui)
   │ submit(Job)            ▲ poll per frame: progress snapshot, completion
   ▼                        │
 JobRunner thread ── runs ONE job at a time (queue depth 1; Run buttons disabled while busy,
   │                        and a second submit returns "busy" instead of queueing silently)
   │ job may create ─► WorkerPool (p std::threads, bounded, joined before job returns)
   │
 DeviceMgr thread ── 10 Hz tick, owns adapters; UI → commands via mutex-guarded queue
```
- **Progress:** `std::atomic<double>` fraction + mutex-protected stage string; UI reads it every frame.
- **Cancellation:** `CancelToken` (atomic bool). Checked per facet-batch (parsers), per case (runners), per mesh (study), per rep (bench). Cancel → job returns `Error{Cancelled}` → **ResultStore unchanged** (atomic publish = `shared_ptr` swap only on success). That's the "no corrupted results" guarantee, and it gets a dedicated test.
- **Exports** write to `file.tmp` then `rename` → never a half-written CSV.
- **WorkerPool:** fixed threads; work = chunk of case indices from `std::atomic<size_t> next` (chunk = max(1, cases/(8p))); each worker writes `out[i]` for its own indices only, so there's no locking in the hot path. Exceptions in workers are captured (`std::exception_ptr`) and rethrown as `Error{Internal}` after join.
- **Why not OpenMP:** MSVC only supports OpenMP 2.0 by default, and an explicit pool is easier to explain in the verification discussion and to test with TSan.

### 3.3 FEA solver internals
- Uniform mesh, `h = L/N`. Element: `Ke = (kA/h)[[1,−1],[−1,1]]`, `fe = (qAh/2)[1,1]`.
- Tridiagonal storage (`a` sub, `b` diag, `c` super, `f` rhs): O(N) memory.
- Dirichlet by symmetric elimination: for node 0 → `f[1] −= a[1]·T0; a[1]=0; b[0]=1; c[0]=0; f[0]=T0` (same at N).
- Thomas algorithm, no pivoting. The reduced K is SPD and diagonally dominant, so it's stable. The doc states this, and a test cross-checks against dense Gaussian elimination with partial pivoting (test-only oracle).
- End heat flows from **unconstrained** equations `K_full·T − f_full` (reactions). Energy balance: `qLeft + qRight = q·A·L` (= 1 W) checked to 1e-10.
- Midpoint error: `T_mid = ½(T_i+T_{i+1})` vs `T_exact(x_mid)`.

### 3.4 Parser internals
- Streamed line reader (64 KB buffer), progress = bytes read / file size, `std::from_chars` for numbers (fast, locale-independent, detects overflow). MSVC supports floating-point `from_chars`.
- STL grammar enforced by a small state machine; binary-STL sniff (no `solid` keyword, or `solid` but size == 84 + 50·n) → `UnsupportedFormat` with the hint "Export as ASCII STL".
- CSV: BOM strip, CRLF/LF, header must match exactly (case-sensitive names, surrounding spaces trimmed); quoted fields rejected with a clear message (not in schema).
- `ImportLimits` default: 512 MB, 5 M facets, 5 M nodes *(est., tuned to measured memory)*; exceeding them → `LimitExceeded` with the measured limit in the hint.

### 3.5 Rendering
- D3D11 device: try `HARDWARE`, fall back to `WARP` (software); the choice is logged and shown in About. Handle `DXGI_ERROR_DEVICE_REMOVED` by recreating the device and buffers.
- Viewport = offscreen render target shown via `ImGui::Image`; MSAA 4× if supported.
- Mesh: flat shading (per-face normal) + headlight; optional wireframe overlay.
- Point cloud: instanced camera-facing quads (vertex shader expands), size slider in px, inferno colormap as 256-texel 1D texture, legend drawn with ImGui (min/max/5 ticks, "°C").
- Picking: CPU, project points to screen, nearest within 6 px (O(n) per click; 1 M points ≈ a few ms *(est.)*, acceptable for a click).
- Camera: orbit about target, pan in view plane, dolly zoom to cursor; Fit = bbox sphere into FOV.
- Shaders compiled at startup via `D3DCompile` (system `d3dcompiler_47.dll`), **or** precompiled to bytecode headers at build time (fxc) to remove the runtime dependency entirely. **Choose precompiled.**

### 3.6 Hardware mock design
- `MockTransport(seed, latencyMs, jitterMs, failMode)`; failure modes: `None`, `TimeoutOnce`, `TimeoutAlways`, `Disconnect`.
- Retry policy (DeviceMgr): timeout 250 ms; up to 3 retries with backoff 100/200/400 ms; success → `Connected` + log "recovered after n retries"; exhausted → `Faulted` with hint "Check cable/power (simulated)".
- Telemetry per kind: MCU `vdd_V`, `die_temp_C`; FPGA `xadc_temp_C`, `done=1`; JTAG `vtref_V`, `idcode=0x…` (mock, labelled).
- Every UI surface showing these values has a **SIMULATED** badge; DeviceInfo.simulated is asserted true in tests.

### 3.7 Backprop module
Pure functions, doubles; `Trainer` returns 101 losses (initial + 100 steps) → CSV + SVG + ImPlot. Gradient check table: analytic, FD, |diff|, pass.

### 3.8 Error handling summary
| Layer | Policy |
|---|---|
| core | No exceptions across the public API; return `Result`. Internal `std::bad_alloc` caught at job boundary → `Error{Internal, "Out of memory", hint: reduce size}` |
| services | Every job wrapped in try/catch → Error; logged with job id + duration |
| app | Top-level `SetUnhandledExceptionFilter` writes `logs/crash_<time>.txt` (+ minidump via system `dbghelp`) and shows a message box |
| launcher | If `app\UniForge.exe` missing or fails to start → message box with the path checked + GetLastError text + "re-extract the ZIP" hint; exit code ≠ 0 |

---

## 4. Scale, performance & reliability

### 4.1 Load estimation
| Workload | Size | Memory *(est.)* | Time *(est.)* |
|---|---|---|---|
| ASCII STL 1 M facets | ≈ 250 MB file | 72 MB (double tris) + 72 MB GPU (float, non-indexed) | parse 2–4 s |
| CSV 1 M nodes | ≈ 60 MB | 40 MB + 16 MB GPU | parse ≈ 1 s |
| FEA single solve N = 80 | trivial | < 10 KB | µs |
| Bench default 4 000 cases × 4 000 elements | 16 M nodal values | 128 MB results per run (serial + parallel ≈ 256 MB) | T1 ≈ 0.3–1 s; tuned so T1 ≥ 1 s |
| Bench UI quick preset 500 × 1 000 | | 4 MB | ≈ 30 ms |

Result buffers are **preallocated outside the timed region**, so timing measures compute only (D4).

### 4.2 Scaling (vertical only; this is a desktop app)
- Parallel scaling limited by: tridiagonal solve is memory-light (fits in L2 at N = 4 000 ≈ 160 KB), so near-linear scaling is expected to 4 P-cores; past that, **hybrid P/E cores** and turbo frequency drop reduce efficiency. Documented, with optional measurements at 8/16 marked "beyond requirement".
- Single-logical-CPU machine: pool still runs with p = 1 path + forced p = 2 demo; report states "speedup not assessable" automatically when `logical == 1`.

### 4.3 Failure modes & redundancy
| Failure | Detection | Handling |
|---|---|---|
| No GPU / old driver | D3D11 HW create fails | WARP fallback, banner "software rendering" |
| GPU reset | DEVICE_REMOVED | recreate device |
| Package dir read-only | write probe at startup | logs/ini → %LOCALAPPDATA%, exports via Save dialog |
| Path with spaces/Unicode | — | wide-char Win32 APIs everywhere, `std::filesystem::path` |
| High DPI / 4K | — | Per-monitor-v2 manifest, font scaled by DPI |
| Huge/malicious file | limits + streaming | LimitExceeded / ParseError, never crash |
| Job exception / OOM | job boundary | Error + log, previous result kept |
| SmartScreen warning | — | Documented in README; version resource + no packer to reduce AV false positives |

### 4.4 Monitoring (logs & metrics)
- Structured log line: `2026-11-03T10:22:04.512Z INFO  job=7 thermal.study done in 12.4 ms N=[10,20,40,80]`.
- Metrics captured automatically: launch-to-ready (process creation time → first `Present`), per-job duration, import throughput (MB/s), render backend, bench report.
- Log panel filter by level + "Export log". The demo walkthrough uses these logs as its timestamp evidence (S2).

---

## 5. Quality, testing & delivery pipeline

```text
dev commit ─► CI (GitHub Actions)
              ├─ windows-msvc : configure (preset) → build Release /W4 /WX → ctest (all labels) → package.ps1 → upload ZIP artefact
              ├─ linux-clang  : core + tests, -fsanitize=address,undefined
              ├─ linux-tsan   : perf + jobs tests, -fsanitize=thread
              └─ lint         : clang-format check, core-dependency rule check, hygiene scan (abs paths/secrets)
release ─► package.ps1 → Windows Sandbox smoke test (manual, scripted checklist + screenshots) → CandidateID_Workbench.zip
```
Test pyramid: ~25 unit, ~20 numerical, ~35 negative, ~8 integration (golden-file CLI diff, UI-less service tests of JobRunner cancel/publish), + manual UI checklist (`docs/test_checklist.md`) with screenshots for things that can't be unit-tested (rotate/pan/zoom, picking).

---

## 6. Trade-off analysis

| Decision | Chosen | Alternative | Trade-off accepted |
|---|---|---|---|
| UI toolkit | Dear ImGui + ImPlot | Qt 6 Widgets | Less native look; in exchange tiny static binary, no DLL deployment or LGPL relinking duties, trivial responsiveness model. Rubric says no framework preference. |
| Graphics API | Direct3D 11 (+WARP) | OpenGL / VTK | Windows-only renderer; in exchange guaranteed launch on driverless VMs (gate). Core stays portable. |
| Process model | Single process, in-proc core | Separate backend process + IPC | No isolation if core crashes; but no "backend not started" failure mode and much simpler. |
| Language level | C++17 | C++20 (jthread, span) | Slightly more boilerplate; matches brief wording exactly. |
| Linear solver | Thomas O(N) | Dense LU / Eigen | Specialised to tridiagonal; dense kept as test oracle; no third-party math dependency. |
| Parallelism | Hand-written bounded pool | OpenMP / std::execution::par | More code (~120 lines); fully explainable, testable, deterministic. |
| Job policy | One job at a time | Concurrent jobs | Less throughput; much simpler state, no benchmark interference (important for timings). |
| CRT | Static /MT | Bundle VC++ redist DLLs | Slightly larger exe; zero redistributable risk. |
| Shaders | Precompiled bytecode | Runtime D3DCompile | Build step needs fxc (in Windows SDK); removes runtime DLL dependency. |
| Plots in report | C++ SVG writer | Python/matplotlib | Less pretty by default; no Python dependency, reproducible from the same binary. |
| Dependencies | Vendored + pinned SHAs | FetchContent at configure | Bigger repo (~10 MB); offline, reproducible builds. |
| Branching | main + phase tags | PR per phase | Less review ceremony; clear linear phase history (fits "phase by phase"). |

---

## 7. Requirement → component → test → evidence (traceability)

| ID | Component | Test(s) | Evidence artefact |
|---|---|---|---|
| A1–A2 | app/views, widgets | manual checklist, ValidatedInput unit | screenshots, demo |
| A3 | JobRunner, ResultStore, ExportService | `jobs.cancel_keeps_previous_result`, `export.atomic_rename` | log excerpt, demo |
| A4 | CMake dep rule | lint job | CI log, architecture.md |
| A5–A6 | launcher, package.ps1, .vscode | Sandbox smoke checklist | sandbox screenshots, launch log |
| B1–B2 | io, render | parser positive tests | screenshots, sample files |
| B3 | io + ImportLimits | ~27 negative fixtures, size-limit benchmark | `results/benchmarks/import_limits.md` |
| B4 | data/ | — | `data/PROVENANCE.md` |
| C1–C3 | thermal | ~12 numerical tests | `results/csv/fea_N*.csv`, `convergence.csv`, plots |
| C4 | docs | — | `docs/thermal_fea.md` (incl. real debugging log) |
| D1–D4 | perf | determinism, agreement, order, cap, cancel; TSan | `results/benchmarks/bench_*.{json,csv,md}`, `docs/performance.md` |
| E1–E2, E4 | docs | — | `docs/embedded_design.md` + diagram SVG |
| E3 | hw + HardwareView | adapter + retry tests | demo, log "recovered after 1 retry" |
| F1 | (you) | — | `docs/appendix_handwritten/*.pdf` |
| F2 | ml | value, FD, update, loss tests | `results/csv/loss_curve.csv`, plot |
| S1–S4 | tools, docs | hygiene scan | ZIP, report.pdf, ai_usage_log.md, ownership.md, effort_log.md, git bundle |

---

## 8. Phase plan (design view)
Same 10 phases as the existing plan (0 Setup → 9 Delivery). The design adds explicit **exit criteria per phase = the traceability rows above turning green**, and puts the Windows Sandbox launch test in Phase 1 *and* Phase 9. Commits under the user's identity only, `phaseN(scope): …`, annotated tag per phase.

## 9. What to revisit as the system grows
- Multiple concurrent jobs → job queue with priorities and per-job cancel.
- Real hardware → replace `MockTransport` with serial (Win32 COM) / libusb / vendor SDK adapters behind the same `IDeviceAdapter`.
- 2D/3D FEA → swap the tridiagonal solver for sparse CSR + CG/Cholesky (Eigen), and the assembler for a generic element loop.
- Mesh-to-result registration (out of scope now) → shared coordinate frame + nearest-node interpolation.
- Much larger files → binary STL, memory-mapped parsing, LOD point rendering.
- Cross-platform UI → ImGui backends for Vulkan/Metal; core already portable.

---

## 10. Differences vs `UniForge_Analysis_and_Development_Plan.md`
Both documents agree on stack, architecture and phases. This design **adds or changes**:
1. **Requirement IDs + traceability matrix** (§1.1, §7) as phase exit criteria.
2. **Concrete NFR targets** (frame time, cancel latency, memory, package size, read-only/Unicode-path launch).
3. **Single taxonomy `ErrorCode` + `hint`** and a no-exceptions-across-core rule.
4. **Concurrency spec**: one-job-at-a-time policy, ResultStore `shared_ptr` atomic publish, atomic export via temp+rename, worker exception propagation.
5. **Portable RNG mapping** (avoid `std::uniform_real_distribution`), so seeds give identical batches on MSVC and clang. This is a real correctness detail the plan missed.
6. **Benchmark memory/time estimates**: default workload changed to 4 000 × 4 000 with preallocated results, instead of 2 000 × 2 000.
7. **Precompiled shaders** (no runtime `d3dcompiler_47.dll` dependency), DEVICE_REMOVED handling, DPI awareness.
8. **CSV + `.meta.json` sidecar** instead of comment lines in CSV; logs/ini in `%LOCALAPPDATA%` when the package dir is read-only.
9. **Crash handler** (log + minidump) and launcher error contract.
10. **CI lint job** enforcing the core-has-no-UI-deps rule and the hygiene scan on every push.
