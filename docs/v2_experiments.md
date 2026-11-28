# V2 experiments: harness and runbook

How the V2 solver comparison is run (build spec §7–§9). The harness is `uniforge_cli study`;
its code is in `src/core/study/` (plan, one run, collect) and `src/cli/commands/StudyCommand.cpp`
(batch, process control).

## What one run does

Each run is identified by an id such as `E2-HS7-s0.500-PCG-J` (experiment, design, parameters,
mesh spacing in mm, solver). The plan is defined in code (`planExperiments`), so the child
process recreates its run from the id alone.

1. Mesh the design at the spacing and assemble K and f (assembly time and matrix size recorded).
2. **Reference runs** (`…-REF`) solve the system once with the direct solver (nested dissection)
   if it fits the 8 GB budget, otherwise with AMG-PCG to a relative residual of 1e-12, and save
   T_ref to `refs/<system>.bin`. Later runs on the same system reuse it.
3. **Solve runs** set up the solver and solve, repeated by run time: 5 repetitions if the first
   takes under 10 s, 3 under 60 s, else 1 (amendment A6). The first repetition carries the
   error observer (‖T_k − T_ref‖∞ per iteration; its time is measured and subtracted). The
   iteration history goes to `history/<id>.csv`.
4. The result row (`runs/<id>.csv`, columns in `resultColumns()`) holds the setup and solve
   status, median/min/max times, iterations, work in SpMV-equivalents, κ estimate, true
   residual, error against T_ref, peak temperature, thermal resistance, the energy audit, solver
   bytes, peak working set, the pinned core, the CPU load before the run and the git commit.

Single-thread runs are pinned to one performance core (amendment A8). On Windows every child runs
in a Job Object with a memory cap (solver budget + 6 GiB) and is killed after its wall-time cap
plus 10 minutes; a run that leaves no result gets a `TimeCap` or `Crashed` row, so no run goes
missing.

## Experiments

| Id | What | Runs (full plan) |
|---|---|---|
| E0 | v1 bar and a cube, 3 meshes each, all six solvers | 42 |
| E1 | Verification: v1 bar, manufactured solution 4–32 cells, single fin | 6 |
| E2 | HS-7 spacing ladder 1.0 → 0.198 mm (56 k → 3.76 M DOF), all six solvers, plus an observer-overhead pair | 57 |
| E3 | HS-7 at 0.5 mm: h = 5 … 500, TIM k = 0.5 … 200, TIM 0.1 … 0.5 mm with 1 or 2 cells; PCG-J, PCG-IC0, AMG, AMG-PCG, DIRECT-ND (one reference each; the h = 25 system is shared) | 72 |
| E4 | Complexity ladder bar, cube, plate, fins, HS-7, pin fin at 1 mm, all six solvers | 42 |
| E5 | Derived from E2 by `collect`: setup + k·solve for k = 1, 5, 20 | 0 |
| E6 | Threads 1 … 20 for PCG-J and AMG-PCG on HS-7 at 0.4 mm | 12 |
| E7 | Natural ordering of the direct solver on HS-7 at 1.0 and 0.794 mm | 2 |
| E8 | Singular case (every face insulated), all six solvers; IC(0) without the shift ladder | 8 |

`uniforge_cli study plan` prints the exact list (241 runs) and writes `manifest.json`.

## Running the official batch

1. Commit everything, then **reconfigure and rebuild Release**, so the git commit recorded in every
   row is the one being measured:

   ```bash
   cmake --preset windows-msvc
   cmake --build --preset windows-release
   ```

2. Close other programs; leave the machine idle and plugged in. The batch keeps Windows awake
   while it runs.

3. Start it from the repository root (it refuses to start on a dirty tree, build spec D-20):

   ```bash
   build/windows-msvc/bin/Release/uniforge_cli.exe study batch --out results/v2
   ```

   Progress is in `results/v2/progress.json` and `results/v2/batch.log`.

4. If it stops (crash, reboot, Ctrl+C), run the same command again: finished runs are skipped.

5. When it ends it runs `study collect`, which writes `results/v2/csv/*.csv` and
   `results/v2/numbers.json`.

The dry run (`study batch --dry-run --out build/study_dry`) exercises the same code on the two
smallest sizes of every sweep with 300 s caps; its timings are not official.

## Official batch, 2026-11-23

Run on commit `5e9bea7`, Release build, machine idle, from 23:06 to 01:52 UTC (2 h 46 min wall
time). **The 72 AMG and AMG-PCG rows were re-run the same day on commit `9be2090`** (07:49–08:01
UTC) after the strength-threshold fix below; every row records its own commit. Files:
`results/v2/runs/` (one row per run), `results/v2/history/` (iteration histories),
`results/v2/csv/` (per experiment, E5 load cases, E2 time-to-accuracy),
`results/v2/numbers.json`, `results/v2/batch_console.txt` and
`results/v2/batch_console_amg_rerun.txt` (console output of the batch and of the AMG re-run). The
reference solutions (`results/v2/refs/`, 87 MB) are not committed; the REF runs recreate them.
The replaced θ₀ = 0.08 rows are in `results/v2_superseded/amg_theta008/` (not official).

**Outcome:** 241 of 241 runs left a result: 228 converged, 7 direct runs were refused by the 8 GB
memory budget before factorising (RCM from 568 k DOF, ND from 1.02 M DOF), and the 6 singular E8
runs were all detected (4 solve breakdowns, 2 AMG setup breakdowns). No run stagnated, crashed,
hit a time cap or was killed by the memory cap. (Before the fix: 205 converged and 23
stand-alone AMG runs stagnated.)

**Harness defects found in the official run** (results unaffected; fixed after the batch):
`batch.log` kept only its first line (the old log was still open when the atomic rename
replaced it, which Windows refuses), and the last `progress.json` update says 240 of 241. The
console output holds the full per-run log.

**Process notes on the AMG re-run.** The 72 old rows were moved out of `results/v2`, so the tree
differed from the commit only in those result files; the batch was started with `--allow-dirty`
after checking that `git diff HEAD -- ':!results/v2'` was empty (the flag only skips that check).
The first re-run skipped the runbook's "reconfigure" step, so its rows recorded the stale
configure-time commit `68a4041`; they were discarded and the 72 runs repeated after
reconfiguring. `batch.log` therefore lists the AMG runs twice; the rows in `runs/` are the second
re-run, all on `9be2090`.

### Hypotheses (build spec §8.1; final wording in V2-10)

| | Result | Evidence |
|---|---|---|
| H1 | **Confirmed for the answers, partly for times.** All six solvers give the same T_max (bar 188.2 °C, cube 100.0 °C) to within 1e-5 K of the reference (largest: AMG 8.5e-6 K on the 10 k DOF bar). On time no family wins everywhere: AMG-PCG wins the smallest bar (0.11 ms), direct RCM the 1.8 k and 10 k DOF bars (0.79 ms and 6.5 ms vs AMG-PCG 1.15 ms and 9.1 ms), PCG-J all three cubes | E0 |
| H2 | **Refuted as stated.** There is no crossover: AMG-PCG is the fastest solver at every HS-7 size, from 56 k DOF (0.19 s vs direct RCM 2.8 s, ND 3.7 s, PCG-IC0 0.29 s) to 3.76 M DOF (9.5 s, 11× ahead of PCG-IC0). Direct never wins on HS-7. PCG-J never wins on time and uses the least memory (1.66 GB at 3.76 M DOF vs PCG-IC0 2.29 GB, AMG-PCG 3.00 GB) | E2 |
| H3 | **Confirmed.** Iteration slopes vs DOF: PCG-J 0.23, PCG-IC0 0.34 (≈ n^{1/3}); AMG-PCG −0.17 (50 → 23) and stand-alone AMG −0.35 (365 → 75): flat or falling, as multigrid should be | E2 |
| H4 | **Partly.** κ_est of the IC(0)-preconditioned system falls 93× from h = 5 to h = 500 but PCG-IC0 iterations change only 361 → 260 (not ∝ √κ_est); TIM conductivity and thickness barely matter for it (337–351). AMG-PCG needs 23–40 iterations over all E3 variants (the most, 40, with two cells across the TIM). Direct cost is unchanged (~70 s) | E3 |
| H5 | **Refuted.** For 20 load cases AMG-PCG still wins at every size (56 k DOF: 3.0 s vs direct RCM 3.4 s; 171 k: 8.3 s vs 27.3 s). Among the other solvers direct beats PCG-IC0 up to 171 k DOF and loses from 286 k, as predicted | E5 |
| H6 | **Supported (indicative).** Factor fill per DOF with ND at 1 mm: plate 194, fins 197, HS-7 296, compact cube 1025 | E4 |
| H7 | **Partly.** PCG-J speeds up 2.6× (14.6 → 5.5 s), flat beyond 4 threads (memory bandwidth), which narrows its gap to serial AMG-PCG (1.57 s) from 9.4× to 3.5× but does not close it. AMG-PCG gains only 1.13× (best 1.38 s at 4 threads) and slows down beyond (2.07 s at 20 threads); the cause is not investigated | E6 |
| H8 | **Not observed.** IC(0) needed no shift anywhere (E3, and without the shift ladder in E8) | E3, E8 |
| H9 | **Confirmed.** All six solvers report the singular case (after the V2-8 dry-run fix) | E8 |
| N4 | Observer overhead: PCG-J with/without the error observer 2.876 vs 2.880 s (0.15 %) | E2 |

Other findings: RCM factorises faster than ND up to 171 k DOF and ND is faster from 286 k
(E2); natural ordering needs 293 s and 8× the fill of ND at 94 k DOF (E7).

## AMG strength threshold: flaw and fix (2026-11-23)

**The flaw.** Smoothed aggregation only groups node j with node i when the coupling is strong,
|a_ij| ≥ θ·√(a_ii a_jj), with θ₀ = 0.08 halved per level. That value was tuned in V2-7 on a
7-point finite-difference Laplacian, where every off-diagonal coupling is 1/6 of the diagonal.
The study's matrices come from trilinear (Q1) hexahedra: for a cell of aspect ratio 1 the axis
neighbours couple with 0, face-diagonal neighbours with 1/16 and body-diagonal neighbours with
1/32 of the diagonal. At θ₀ = 0.08 every coupling of a cubic cell is weak, its nodes join no
aggregate and get no coarse-grid correction, and AMG falls back to Gauss–Seidel smoothing.

**How it was found.** The official batch showed stand-alone AMG stagnating even on the plain cube
(1992 cycles at 110 k DOF) and AMG-PCG iterations growing 17× with refinement on HS-7 (52 → 872),
the opposite of what multigrid should do (H3). HS-7 cells become more cubic as the mesh is refined
(maximum aspect ratio 10 → 3.9), which matched AMG getting worse; the aggregate render P9 showed
that at 1 mm only 0.6 % of HS-7 nodes were isolated, because stretched cells keep strong
couplings along their short edges. A candidate run of all 72 AMG runs at θ₀ = 0.02 (temporary
source change, outside `results/v2`) converged everywhere. The user then decided to adopt it as a
bug fix and re-run the rows officially.

**The fix** (commit `9be2090`): default θ₀ = 0.02 in `SolverOptions`. A new unit test on a Q1
Laplacian shows that θ₀ = 0.08 aggregates no node at all, while 0.02 converges in 12, 13 and 17
cycles on 10³, 16³ and 24³ grids; the old 7-point test keeps θ₀ = 0.08, the value it was tuned on.

**Before and after** (same 72 runs; before: `5e9bea7`, after: `9be2090`; full table in
`results/v2_superseded/amg_theta008/compare_theta008_vs_002.csv`):

| Run | θ₀ = 0.08 | θ₀ = 0.02 |
|---|---|---|
| All 72 AMG / AMG-PCG runs | 47 converged, 23 stagnated, 2 singular | 70 converged, 2 singular |
| E4 cube 1 mm (110 k DOF), AMG cycles / total s | 1992 / 12.9 | 15 / 0.22 |
| E4 bar 1 mm (121 k DOF), AMG | stagnated (2510 / 17.9 s) | 20 / 0.23 s |
| E2 HS-7 1.0 mm (56 k DOF), AMG-PCG iterations / total s | 52 / 0.25 | 50 / 0.19 |
| E2 HS-7 0.5 mm (286 k DOF), AMG-PCG | 292 / 6.78 s | 31 / 0.86 s |
| E2 HS-7 0.198 mm (3.76 M DOF), AMG-PCG | 872 / 237.0 s | 23 / 9.52 s |
| E2 HS-7 0.198 mm, stand-alone AMG | stagnated (2000 / 517 s) | 75 / 23.7 s |
| E6 HS-7 0.397 mm, AMG-PCG serial | 415 / 18.5 s | 28 / 1.57 s |

Errors against the references after the fix are 1.7e-11 to 1.7e-5 K, and operator complexity is
1.00–1.08 (the hierarchies have fewer levels: 4 instead of 6 on the largest HS-7 meshes, because
coarsening is now faster).

**What changed in the conclusions:** H3 is now confirmed (AMG-PCG iterations fall with
refinement); H2 and H5 have no direct/iterative crossover because AMG-PCG wins at every size and
load-case count; H7 is only partly true. P16 (decision chart) shows AMG-PCG in every cell.

**What it does not change:** the V2-7 note that the thin TIM is hard for stand-alone AMG (risk
K2) still shows at 1 mm: stand-alone AMG needs 365 cycles on HS-7 against 15 on the cube.
V2-7 also reported, from a single preliminary run at 2 mm, that θ₀ = 0.02 did not help there; that
run is not part of the plan and was not repeated. As a preconditioner AMG is not affected.

## Figures (V2-9)

`uniforge_cli study figures` writes P1–P22 (build spec §12.1) from `results/v2/csv` and
`results/v2/history` into `docs/report_v2/fig`, with `figures.md` listing the data behind each
file. Renders (P1–P6, P8, P9, P15) come from small in-process solves of the same designs; the
plots read the CSVs only, so the figures follow the results when they are re-run. All figures
take about 16 s.
