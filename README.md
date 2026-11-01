# UniForge: Unified Engineering Workbench

UniForge is a small Windows desktop workbench that imports engineering geometry and
thermal results, solves a 1D thermal finite-element problem, benchmarks serial vs parallel
batch solves, and demonstrates simulated embedded-hardware adapters. All backend logic,
numerics, file processing and concurrency are written in C++17.

> **Status: Phase 0 (setup).** The build skeleton and design documents are in place;
> features land phase by phase (see [CHANGELOG.md](CHANGELOG.md)).

## Planned features

| Area | Task | What it does |
|---|---|---|
| Interface & delivery | A | One app with Geometry, Thermal Analysis, Performance and Hardware views; one-click `Launch.exe` |
| CAD / ANSYS data | B | ASCII STL viewer and nodal-temperature CSV point cloud with validation |
| Thermal FEA | C | Steady 1D rod solver with two-node linear elements, verified against the analytical solution |
| Multicore | D | Serial vs bounded-thread-pool batch solves with a reproducible benchmark |
| Embedded design | E | MCU / FPGA / JTAG integration design and simulated C++ device adapters |
| Backpropagation | F | Hand-derived gradients for a one-neuron network, checked by finite differences |

## Prerequisites (source build)

- Windows 11 x64
- Visual Studio 2022 Build Tools (workload: *Desktop development with C++*, MSVC v143, Windows 11 SDK)
- CMake ≥ 3.25. The copy bundled with the Build Tools works; it is on `PATH` inside
  *Developer PowerShell for VS 2022*, or add
  `<BuildTools>\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin` to `PATH`.

The packaged application needs none of these; it runs from `Launch.exe` after extracting the ZIP.

## Build from source

```bash
cmake --preset windows-msvc
cmake --build --preset windows-release
ctest --preset windows-release
```

Linux (core library and tests only, used by CI):

```bash
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
```

## Repository layout

```text
cmake/        shared compiler options and warnings
src/          C++ sources (core library, app, CLI, launcher), added from Phase 1
tests/        unit, numerical, negative and integration tests
data/         sample STL / CSV inputs with provenance
results/      generated evidence: CSV, plots, benchmarks
docs/         design documents, reports, AI-use and effort logs
tools/        packaging scripts (no backend logic)
```

## Documentation

- [Final build specification](docs/design/UniForge_Final_Build_Spec.md)
- [AI-use log](docs/ai_usage_log.md)
- [Effort log](docs/effort_log.md)

## Licence

MIT, see [LICENSE](LICENSE). Third-party components are listed in [DEPENDENCIES.md](DEPENDENCIES.md).
