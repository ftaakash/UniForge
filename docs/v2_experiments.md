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
