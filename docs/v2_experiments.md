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

Run on commit `5e9bea7` (recorded in every row), Release build, machine idle, from 23:06 to
01:52 UTC (2 h 46 min wall time). Files: `results/v2/runs/` (one row per run),
`results/v2/history/` (iteration histories), `results/v2/csv/` (per experiment, E5 load cases, E2
time-to-accuracy), `results/v2/numbers.json`, `results/v2/batch_console.txt` (the batch's console
output). The reference solutions (`results/v2/refs/`, 87 MB) are not committed; the REF runs
recreate them.

**Outcome:** 241 of 241 runs left a result: 205 converged, 23 stand-alone AMG runs stagnated,
7 direct runs were refused by the 8 GB memory budget before factorising (RCM from 568 k DOF,
ND from 1.02 M DOF), and the 6 singular E8 runs were all detected (4 solve breakdowns, 2 AMG
setup breakdowns). No run crashed, hit a time cap or was killed by the memory cap.

**Harness defects found in the official run** (results unaffected; fixed after the batch):
`batch.log` kept only its first line (the old log was still open when the atomic rename
replaced it, which Windows refuses), and the last `progress.json` update says 240 of 241. The
console output holds the full per-run log.

### First reading of the hypotheses (build spec §8.1; figures and final wording in V2-9/V2-10)

| | Result | Evidence |
|---|---|---|
| H1 | **Confirmed for the answers, partly for times.** All six solvers give the same T_max (bar 188.2 °C, cube 100.0 °C) to within 1e-5 K of the reference. On time, direct wins only the 10 k DOF bar (0.029 s vs 0.033 s for PCG-IC0); AMG-PCG wins the two smaller bars and PCG-J the cube | E0 |
| H2 | **Refuted as stated.** On HS-7, direct ND never wins a single solve: at 56 k DOF it takes 3.7 s vs 0.29 s for PCG-IC0, so n₁ (one load case) is below the smallest E2 mesh. PCG-J never wins on time and uses the least memory (1.66 GB at 3.76 M DOF vs 2.29 GB for PCG-IC0) | E2 |
| H3 | **PCG confirmed, AMG refuted.** Iteration slopes vs DOF: PCG-J 0.23, PCG-IC0 0.34 (≈ n^{1/3}); AMG-PCG 0.64 (52 → 872), not flat. See the AMG note below | E2 |
| H4 | **Partly.** κ_est falls 93× from h = 5 to h = 500 but PCG-IC0 iterations change only 361 → 260 (not ∝ √κ_est); TIM conductivity and thickness barely matter (337–351). Direct cost is unchanged (~70 s) | E3 |
| H5 | **Confirmed.** For 20 load cases, direct wins up to 171 k DOF (RCM 27.3 s vs PCG-IC0 30.4 s) and loses at 286 k (ND 73.4 s vs 63.4 s) | E5 |
| H6 | **Supported (indicative).** Factor fill per DOF with ND at 1 mm: plate 194, fins 197, HS-7 296, compact cube 1025 | E4 |
| H7 | **Confirmed.** PCG-J speeds up 2.6× (14.6 → 5.5 s), flat beyond 4 threads (memory bandwidth); with all cores it beats every serial iterative solver on the same mesh (PCG-IC0 8.2 s, AMG-PCG 18.0 s) | E6 |
| H8 | **Not observed.** IC(0) needed no shift anywhere (E3, and without the shift ladder in E8) | E3, E8 |
| H9 | **Confirmed.** All six solvers report the singular case (after the V2-8 dry-run fix) | E8 |
| N4 | Observer overhead: PCG-J with/without the error observer 2.876 vs 2.880 s (0.15 %) | E2 |

Other findings: RCM factorises faster than ND up to 171 k DOF and ND is faster from 286 k
(E2); natural ordering needs 293 s and 8× the fill of ND at 94 k DOF (E7).

**AMG note (suspected implementation weakness, not yet a conclusion).** Stand-alone AMG
stagnates even on the plain cube (1992 cycles at 110 k DOF), and AMG-PCG iterations grow with
refinement. A likely cause is the strength threshold θ₀ = 0.08: for trilinear hexahedra of
aspect ratio 1 the largest off-diagonal coupling is 1/16 of the diagonal (edge neighbours;
axis neighbours couple with zero), so every coupling counts as weak, the node is "isolated" and
gets no coarse-grid correction. HS-7 cells become more uniform as the mesh is refined (maximum
aspect ratio 10 → 3.9), which matches AMG getting worse with refinement. The V2-7 tuning used a
7-point finite-difference Laplacian, where the ratio is 1/6. This is checked separately as a
candidate change; the official results above stay as measured.
