# Superseded: AMG rows at θ₀ = 0.08 (official batch of 2026-11-23, commit `5e9bea7`)

These 72 rows (stand-alone AMG and AMG-PCG, all experiments) were measured in the official batch
with the strength threshold θ₀ = 0.08 from the V2-7 tuning. That value was wrong for trilinear
hexahedra (see `docs/v2_experiments.md`, "AMG strength threshold: flaw and fix"). They were
replaced in `results/v2` by the same 72 runs at θ₀ = 0.02 (commit `9be2090`) and are kept here
only as the "before" of that fix. They are not official results.

- `runs/`, `history/`: the rows and iteration histories exactly as measured.
- `compare_theta008_vs_002.csv`: one line per run, before (`_008`) and after (`_002`): status,
  iterations, total seconds, levels, operator complexity, error vs reference, commit.
