# V3 report figures

Written by `uniforge_cli study figures --only V1,...` from `results/v3`. Every figure maps to the data it is drawn from.

| Figure | File | What it shows | Drawn from |
|---|---|---|---|
| V1 | [V1_speedup_vs_dof.svg](V1_speedup_vs_dof.svg) | AMG-PCG-CH-SP against V2's AMG-PCG on one core (HS-7, pinned, V3H) | results/v3/csv/V3H.csv |
| V2 | [V2_timing_scatter.svg](V2_timing_scatter.svg) | One solve, 21 runs: AMG-PCG-CH-SP, HS-7 3.76 M DOF, 8 threads (V3G) | results/v3/csv/V3G_timing.csv |
| V3 | [V3_external_references.svg](V3_external_references.svg) | One core: our AMG-PCG against hypre and PETSc on HS-7 (V3F, untuned) | results/v3/csv/V3F.csv, results/v3/csv/V3F_petsc.csv |
| V4 | [V4_load_cases.svg](V4_load_cases.svg) | Several load cases after one setup (HS-7, V3E) | results/v3/csv/V3E.csv |
| V5 | [V5_gmg_vs_amg.svg](V5_gmg_vs_amg.svg) | Geometric vs algebraic multigrid on HS-7 (above 1: GMG faster) | results/v3/csv/V3D.csv, results/v3/csv/V3H.csv |
