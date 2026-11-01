# UniForge: Unified Engineering Workbench

UniForge is a small Windows desktop workbench that imports engineering geometry and
thermal results, solves a 1D thermal finite-element problem, benchmarks serial vs parallel
batch solves, and demonstrates simulated embedded-hardware adapters. All backend logic,
numerics, file processing and concurrency are written in C++17.

> **Status: Phase 1 (walking skeleton + packaging).** The app shell, launcher, background job
> runner and packaging pipeline work end to end; the task views are placeholders that fill in
> phase by phase (see [CHANGELOG.md](CHANGELOG.md)).

## Features

| Area | Task | What it does | Status |
|---|---|---|---|
| Interface & delivery | A | One app with Overview, Geometry, Thermal Analysis, Performance, Hardware and Backprop views; one-click `Launch.exe` | Shell, launcher, job runner: Phase 1 |
| CAD / ANSYS data | B | ASCII STL viewer and nodal-temperature CSV point cloud with validation | Phases 3–4 |
| Thermal FEA | C | Steady 1D rod solver with two-node linear elements, verified against the analytical solution | Phase 2 |
| Multicore | D | Serial vs bounded-thread-pool batch solves with a reproducible benchmark | Phase 5 |
| Embedded design | E | MCU / FPGA / JTAG integration design and simulated C++ device adapters | Phase 6 |
| Backpropagation | F | Hand-derived gradients for a one-neuron network, checked by finite differences | Phase 7 |

## Run the packaged app (no build needed)

1. Extract `CandidateID_Workbench.zip` anywhere (paths with spaces or non-ASCII characters are fine).
2. Double-click `Launch.exe`.

No installer, administrator rights, internet connection, VC++ redistributable or GPU driver is
needed: the executables link the runtime statically, and rendering falls back to WARP (Windows'
software rasteriser) when no hardware Direct3D 11 device is available. The status bar then shows
"Software rendering".

- **SmartScreen:** the executables are unsigned, so Windows may show "Windows protected your PC"
  on first launch. Choose *More info → Run anyway*.
- **VS Code:** open the extracted folder. *Terminal → Run Build Task* (`Ctrl+Shift+B`) runs
  "Run UniForge (packaged)" with no extension needed; *Run and Debug → Run UniForge (packaged)*
  does the same and needs the C/C++ extension (`ms-vscode.cpptools`).
- **Logs:** `logs/uniforge.log` and `logs/launch.log` next to `Launch.exe`. If that folder is
  read-only, logs and `imgui.ini` go to `%LOCALAPPDATA%\UniForge\` instead.

### Keyboard

| Keys | Action |
|---|---|
| `Ctrl+1` … `Ctrl+7` | Overview, Geometry, Thermal, Performance, Hardware, Backprop, About |
| `Ctrl+L` | Show or hide the log panel |
| `Ctrl+Enter` / `Esc` | Run / cancel the job of the current view |

### Command-line options of `app\UniForge.exe` (diagnostics)

| Option | Effect |
|---|---|
| `--launch-t0=<QPC>` | Passed by `Launch.exe` to measure launch-to-ready |
| `--force-warp` | Skip the hardware device and use WARP (tests the clean-machine path) |
| `--simulate-device-lost` | Treat one Present as `DXGI_ERROR_DEVICE_REMOVED` (tests recovery) |
| `--theme=light` / `--theme=dark` | Override the Windows app-mode theme |

## Prerequisites (source build)

- Windows 11 x64
- Visual Studio 2022 Build Tools (workload: *Desktop development with C++*, MSVC v143, Windows 11 SDK)
- CMake ≥ 3.25. The copy bundled with the Build Tools works; it is on `PATH` inside
  *Developer PowerShell for VS 2022*, or add
  `<BuildTools>\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin` to `PATH`.

All libraries and fonts are vendored in `third_party/`; the build needs no internet access.

## Build from source

```bash
cmake --preset windows-msvc
cmake --build --preset windows-release
ctest --preset windows-release
```

The executables land in `build/windows-msvc/bin/Release/`. Run one test label with
`ctest --preset windows-release -L unit`.

## Package

```bash
powershell -ExecutionPolicy Bypass -File tools/package.ps1
```

This configures, builds Release, runs the tests, stages `dist/CandidateID_Workbench/`, checks with
`dumpbin /dependents` that the executables import only Windows DLLs, runs the hygiene scan
(`tools/hygiene_scan.ps1`: absolute paths, e-mail addresses, secrets) and writes
`dist/CandidateID_Workbench.zip`. The repository's VS Code configuration has "Run packaged (dist)"
to start that staged copy.

Linux (core library and tests only, used by CI from Phase 5):

```bash
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
```

## Repository layout

```text
cmake/          compiler options, warnings, HLSL shader compilation
third_party/    Dear ImGui, ImPlot, GoogleTest, fonts (pinned; see DEPENDENCIES.md), patches/
LICENSES/       third-party licence texts
src/core/       portable C++17 engineering core (standard library only)
src/app/        UniForge.exe: platform/ (Win32, D3D11), services/ (JobRunner, LogSink,
                ResultStore), ui/ (theme, fonts, shell, widgets), views/, shaders/
src/launcher/   Launch.exe (starts app\UniForge.exe; no application logic)
tests/          unit, numerical, negative and integration tests (GoogleTest + CTest labels)
tools/          package.ps1, hygiene_scan.ps1, ZIP-specific VS Code config (no backend logic)
docs/           design documents, AI-use and effort logs, demo screenshots
```

## Known limitations

- Unsigned executables trigger SmartScreen on first launch (see above).
- No screen-reader support: Dear ImGui exposes no UI Automation tree.
- The embedded fonts cover Latin, Greek and the symbols the UI uses; other scripts (e.g. CJK
  characters in a folder name shown on the About page) display as `?`. File access itself works
  with any Unicode path.

## Documentation

- [Final build specification](docs/design/UniForge_Final_Build_Spec.md)
- [Dependencies and licences](DEPENDENCIES.md)
- [AI-use log](docs/ai_usage_log.md)
- [Effort log](docs/effort_log.md)

## Licence

MIT, see [LICENSE](LICENSE). Third-party components are listed in [DEPENDENCIES.md](DEPENDENCIES.md).
