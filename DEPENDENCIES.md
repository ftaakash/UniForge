# Dependencies

Every third-party component is listed here with its exact version, source and licence.
Vendored code lives in `third_party/`; licence texts are in `LICENSES/` and ship in the package.

## Toolchain (build-time only, not redistributed)

| Tool | Version | Notes |
|---|---|---|
| Visual Studio 2022 Build Tools | 17.14.37827.9 | Desktop C++ workload |
| MSVC toolset (v143) | 14.44.35207 | `cl.exe` x64 host and target |
| Windows SDK | 10.0.22621.0 | Win32, Direct3D 11, DXGI headers and libs |
| CMake | 3.31.6 (bundled with Build Tools); ≥ 3.25 required | Presets version 6 |
| GitHub Actions runners | `windows-2022`, `ubuntu-24.04` (GCC 13) | CI only (from Phase 1) |

## Libraries (vendored)

| Name | Version / commit | Source | Licence | Used for |
|---|---|---|---|---|
| _none yet_ | | | | Dear ImGui, ImPlot, GoogleTest and fonts are vendored in Phase 1 |

## Runtime

The packaged executables link the MSVC runtime statically (`/MT`) and depend only on
DLLs that ship with Windows 11 (verified with `dumpbin /dependents` from Phase 1).
