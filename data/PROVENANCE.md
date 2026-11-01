# Sample data: provenance, units, schema, licence

All three files are **self-created** by UniForge's own generator. None of them comes from a CAD
package, ANSYS or any third party.

> **The thermal CSV is SYNTHETIC. It is not an ANSYS export.** Its temperatures are computed from
> the Task C analytical solution plus a made-up lateral variation and noise (below). The file name
> carries `SYNTHETIC`, and the importer turns that into `syntheticFlag`, which the GUI shows as a
> "SYNTHETIC DATA" chip (Phase 4).

| File | What it is | Size |
|---|---|---|
| `geometry/rod_10x10mm_1m.stl` | The Task C rod: a 1 m × 10 mm × 10 mm bar, cross-section A = 1e-4 m² | 12 facets, 1.8 kB |
| `geometry/heatsink_fins.stl` | A small finned heat sink, for a richer 3D view | 96 facets, 13.6 kB |
| `thermal/rod_nodal_temperature_SYNTHETIC.csv` | Nodal temperatures on the rod: **SYNTHETIC** | 909 nodes, 25.0 kB |

## How to regenerate

```bash
app\uniforge_cli.exe gen-samples            # writes data/geometry/*.stl and data/thermal/*.csv
app\uniforge_cli.exe gen-samples --data DIR # same files into another folder
```

(`app\uniforge_cli.exe all`, and therefore `regenerate_evidence.cmd`, also runs `gen-samples`.)
The generator is `src/cli/commands/SamplesCommand.cpp`. Output is byte-identical on every run
and compiler: numbers are written with the shortest round-trip format (`std::to_chars`), the
noise uses `std::mt19937_64` (fully specified by the C++ standard) with a fixed seed and an
explicit `[0, 1)` mapping, and lines end in LF. The integration test
`integration.cli_samples_match_data` regenerates the files and compares them byte for byte
with the copies committed here, then imports each one.

| Generator setting | Value |
|---|---|
| Command | `uniforge_cli gen-samples` |
| Seed | `20261101` (`kSampleSeed`) |
| Random generator | `std::mt19937_64`, `u = (r >> 11) · 2⁻⁵³` in [0, 1) |

## Units

- Lengths: **metres** in both formats. ASCII STL has no unit field; UniForge treats STL
  coordinates as metres, and these files are written in metres.
- Temperature: **degrees Celsius** (column `temperature_C`).

## `geometry/rod_10x10mm_1m.stl`

- ASCII STL, one `solid rod_10x10mm_1m`, 12 triangles (2 per face of a box).
- x from 0 to 1 m (rod axis), y and z from −0.005 to +0.005 m. The x axis is the 1D model's
  coordinate in Task C, so x = 0 is the T0 = 100 °C end and x = 1 m the TL = 20 °C end.
- Closed, outward normals, counter-clockwise vertex order seen from outside. `gen-samples`
  checks the enclosed volume: exactly A · L = 1e-4 m³.

## `geometry/heatsink_fins.stl`

- ASCII STL, one `solid heatsink_fins`, 96 triangles = 8 boxes × 12.
- Base plate 60 × 40 × 4 mm (x × y × z, from the origin); 7 fins, each 2 mm thick (x), 40 mm
  long (y) and 26 mm tall (z = 4 … 30 mm), at x = 3, 12, 21, … 57 mm (pitch 9 mm).
- Each box is a separate closed shell. The fins touch the plate but are not fused with it, so
  the mesh is not one manifold solid; that is fine for viewing, not for meshing or 3D printing.
  Enclosed volume (checked): 2.416e-5 m³.

## `thermal/rod_nodal_temperature_SYNTHETIC.csv` (SYNTHETIC)

**Schema** (the only one the importer accepts, spec B2):

```text
node_id,x_m,y_m,z_m,temperature_C
```

| Column | Type | Unit | Meaning |
|---|---|---|---|
| `node_id` | integer, unique | – | node number, 1 … 909 |
| `x_m`, `y_m`, `z_m` | decimal | m | node position |
| `temperature_C` | decimal | °C | nodal temperature, ≥ −273.15 |

Plain comma-separated text, no quotes, `.` as decimal separator, header on line 1, LF line ends.
The importer also accepts a UTF-8 BOM, CRLF line ends, spaces around values and blank lines.

**Nodes:** 101 stations along the rod (x = 0, 0.01, …, 1 m) × a 3 × 3 grid over the
cross-section (y, z ∈ {−0.005, 0, +0.005} m) = 909 nodes, numbered
`id = 1 + 9·i + 3·j + k` (i = station, j = y index, k = z index).

**Temperatures:**

```text
T(x, y, z) = T_exact(x) − 0.5 °C · ((y/a)² + (z/a)²) / 2 + noise,      a = 0.005 m
T_exact(x) = 100 + 420 x − 500 x²   (Task C: L = 1 m, k = 10 W/(m·K), q = 1e4 W/m³, T0 = 100 °C, TL = 20 °C)
noise      = 0.02 °C · (2u − 1), u from the generator above (one draw per node, in id order)
```

then rounded to 0.0001 °C. The lateral term makes the surface slightly cooler than the core
(−0.25 °C at face centres, −0.5 °C at the edges) so the point cloud shows some variation over
the cross-section. **It is decorative, not physics:** the Task C model is 1D with an insulated
surface, so it predicts no lateral variation at all. The noise imitates the small scatter of
exported results. Neither is calibrated against anything real.

Resulting range: 19.4867 … 188.1923 °C. The hottest centre-line node is at x = 0.42 m, where the
exact peak is 188.2 °C. `gen-samples` checks that every node is within 0.02 °C (+ rounding)
of `T_exact(x)` plus the lateral term, and the test `SampleData.*` re-checks the committed file.

## Tested import limits

Measured with `uniforge_cli import-bench` (temporary generated files, each parsed 3 times in a
fresh process; full table in [`results/benchmarks/import_limits.md`](../results/benchmarks/import_limits.md)).
Machine: Intel Core i5-14600K, 32 GB RAM, Windows 11, MSVC 19.44 Release.

| Format | Tested size | File | Parse time | Peak memory (working set) |
|---|---:|---:|---:|---:|
| ASCII STL | 10,000 facets | 2.5 MB | 0.010 s | 6.1 MB |
| ASCII STL | 100,000 facets | 24.8 MB | 0.10 s | 20.9 MB |
| ASCII STL | 1,000,000 facets | 248 MB | 1.0 s | 123 MB |
| ASCII STL | **2,000,000 facets (limit)** | 496 MB | 2.0 s | 270 MB |
| CSV | 10,000 nodes | 0.5 MB | 0.002 s | 5.2 MB |
| CSV | 100,000 nodes | 5.3 MB | 0.025 s | 13.4 MB |
| CSV | 1,000,000 nodes | 54 MB | 0.24 s | 68 MB |
| CSV | **5,000,000 nodes (limit)** | 275 MB | 1.3 s | 318 MB |

The importers reject anything larger with a clear message (`LimitExceeded`, the limit in the
hint) instead of loading part of it. Limits (`src/core/io/ImportLimits.hpp`): **512 MiB per
file, 2,000,000 STL facets, 5,000,000 CSV nodes, 4,096 characters per line.** The facet and
node limits are the largest sizes measured; 512 MiB is just above the largest file measured
(496 MB). Larger files would probably still load on a machine like this one, but they are
untested, so they are refused.

## Licence

Self-created by the repository author with UniForge's generator; released under the
repository's MIT licence (see [`LICENSE`](../LICENSE)). No third-party data is included.
