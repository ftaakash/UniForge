# V3 experiments

V3 tests the candidates from the post-V2 literature survey that might beat AMG-PCG on HS-7.
Each experiment uses the V2 harness (`uniforge_cli study batch --only <EXP> --out results/v3`),
the V2 tolerance (relative residual 1e-10), 5 timed repetitions (median reported) and the V2
results as the baseline. Branch `v3`, cut from `v2.0.0`.

## V3A: single-precision AMG hierarchy (AMG-PCG-SP)

**Hypothesis.** The literature reports 1.1–1.6× from storing and applying the AMG
preconditioner in single precision under a double-precision Krylov solver, with convergence
barely affected (survey refs. S4). The AMG V-cycle is bandwidth-bound, and single precision
halves the value bytes.

**Implementation** (commit `2fc4749`, `src/core/solvers/amg/Amg.cpp`). `SolverOptions::
amgSinglePrecision` keeps float copies of every level's operator values (level 0 included) and
of P and R. These share the double patterns. The double transfer values and intermediate coarse
operators are released. The coarsest Cholesky and the outer CG stay in double, and row sums are
accumulated in double. Stand-alone AMG ignores the option. Test
`Amg.SinglePrecisionHierarchyKeepsCgAccuracy` (20³ Q1): 10 iterations in both precisions,
bytes per V-cycle 7.82e6 → 5.38e6 (−31 %).

**Runs.** HS-7 ladder k = 0…7 (56 k to 3.76 M unknowns), 1 pinned performance core, error
observer on (as E2). Also k = 5, 6, 7 with 8 and 20 threads, observer off (as E6).
36 runs, all Converged, commit `6abcfe9` (`results/v3/csv/V3A.csv`). The reference solutions
in `results/v3/refs` were computed in an earlier pass of the same code whose rows recorded a
stale build hash. Those rows were discarded and every run was repeated.

### One core (solve time, median of 5)

| DOF | AMG-PCG (s) | AMG-PCG-SP (s) | change | iterations | solver memory (MB) |
|---|---|---|---|---|---|
| 56,015 | 0.162 | 0.176 | +8.6 % (slower) | 50 / 50 | 10 → 13 |
| 93,921 | 0.259 | 0.272 | +5.0 % (slower) | 42 / 42 | 17 → 22 |
| 170,634 | 0.411 | 0.399 | −2.9 % | 35 / 35 | 30 → 40 |
| 285,532 | 0.635 | 0.613 | −3.5 % | 31 / 31 | 52 → 68 |
| 568,152 | 1.178 | 1.117 | −5.2 % | 28 / 28 | 109 → 140 |
| 1,022,979 | 2.013 | 1.899 | −5.7 % | 26 / 26 | 197 → 254 |
| 1,867,768 | 3.720 | 3.574 | −3.9 % | 26 / 26 | 365 → 470 |
| 3,759,720 | 6.701 | 6.404 | −4.4 % | 23 / 23 | 747 → 960 |

Setup is 6–13 % longer (the float copy). At 3.76 M unknowns setup plus solve goes from 9.51 s
to 9.40 s (−1.1 %). The error against the reference is unchanged in order of magnitude at every
size (e.g. 2.0e-11 vs 2.1e-11 K at 3.76 M). Peak process memory is unchanged (2,998 vs
3,000 MB) because the system matrix and mesh dominate it.

### 8 and 20 threads (solve time, median of 5)

| DOF | threads | AMG-PCG (s) | AMG-PCG-SP (s) | change |
|---|---|---|---|---|
| 1,022,979 | 8 | 2.666 | 2.159 | −19 % |
| 1,022,979 | 20 | 2.539 | 2.057 | −19 % |
| 1,867,768 | 8 | 4.791 | 3.915 | −18 % |
| 1,867,768 | 20 | 4.456 | 3.877 | −13 % |
| 3,759,720 | 8 | 7.394 | 6.472 | −12 % |
| 3,759,720 | 20 | 6.963 | 6.259 | −10 % |

Every threaded run is slower than the same solver on one pinned core (e.g. 7.39 s on 8 threads
against 6.70 s on one core at 3.76 M). The Gauss–Seidel smoother is serial, and with more than
one thread the process is not pinned, so the serial sweeps may run on an efficiency core. This
is an inference, not measured per core.

### Verdict

AMG-PCG-SP does **not** clearly beat AMG-PCG on this machine. It is at most 5.7 % faster on one
core, slower below 0.1 M unknowns, and needs 28 % more solver memory. It stays an option, not
the default. The single-core result is far below the 1.1–1.6× in the literature. That means the
V-cycle here is limited by the serial Gauss–Seidel sweep (latency and dependency chain), not by
memory bandwidth. With threads, where the parallel kernels matter more, the saving is 10–19 %.

**Consequence for the next tests.** The serial smoother is the bottleneck for both precisions
and for threading. The next candidate is therefore a parallel smoother (Chebyshev–Jacobi or
multicolour Gauss–Seidel) inside the same AMG. That is also the smoother the geometric-multigrid
test (V3 step 2) needs.

## V3B: Chebyshev–Jacobi smoother (AMG-PCG-CH, AMG-PCG-CH-SP)

**Hypothesis.** V3A showed that the serial Gauss–Seidel sweep limits AMG-PCG, both in single
precision and with threads. A polynomial smoother uses only matrix–vector products and vector
updates, so it runs on every thread (Adams, Brezina, Hu and Tuminaro, J. Comput. Phys. 188,
2003). It moves more bytes per cycle than Gauss–Seidel. It can only win when threads pay for
that.

**Implementation** (commit `ecbf093`, `src/core/solvers/amg/Amg.cpp`). `SolverOptions::
amgSmoother = Chebyshev`: a degree-2 Chebyshev polynomial in D⁻¹A on [λ̂/30, λ̂], with λ̂ = 1.1 ×
the Rayleigh quotient after 20 power iterations from a fixed pseudo-random start. The same
smoother runs before and after the coarse correction, so the V-cycle stays symmetric. Every
pass is a chunked parallel loop, so the result is bit-identical for any thread count. It works
in double (AMG-PCG-CH) and in the V3A single-precision hierarchy (AMG-PCG-CH-SP). Test
`Amg.ChebyshevSmootherConvergesAndIsThreadIndependent`: λ̂ within the Gershgorin bound,
symmetric V-cycle, identical iterates on 1 and 4 threads, and 16 CG iterations at both 16³ and
28³ (Q1).

**Runs.** HS-7 ladder k = 0…7, 1 pinned core, observer on: AMG-PCG (Gauss–Seidel) and
AMG-PCG-CH. k = 4…7 with 8 and 20 threads, observer off: AMG-PCG-CH and AMG-PCG-CH-SP. 40 runs,
all Converged, commit `ecbf093` (`results/v3/csv/V3B.csv`).

### One core: Chebyshev against Gauss–Seidel

| DOF | GS solve (s) | CH solve (s) | CH / GS | iterations GS / CH |
|---|---|---|---|---|
| 56,015 | 0.160 | 0.185 | 1.15 | 50 / 38 |
| 93,921 | 0.263 | 0.271 | 1.03 | 42 / 31 |
| 170,634 | 0.431 | 0.437 | 1.01 | 35 / 26 |
| 285,532 | 0.635 | 0.756 | 1.19 | 31 / 26 |
| 568,152 | 1.166 | 1.617 | 1.39 | 28 / 27 |
| 1,022,979 | 1.993 | 2.882 | 1.45 | 26 / 26 |
| 1,867,768 | 3.743 | 5.527 | 1.48 | 26 / 27 |
| 3,759,720 | 6.637 | 10.915 | 1.64 | 23 / 26 |

On one core Chebyshev is never faster. It needs the same or fewer iterations, and up to 1.64×
the time, because each smoothing step reads the matrix twice. The error against the reference
is the same order at every size (e.g. 1.5e-11 vs 2.0e-11 K at 3.76 M).

### Threads: against V2's AMG-PCG on one core (the V2 best)

Speed-up = (AMG-PCG, 1 core) / (variant). "Total" includes setup.

| DOF | variant | threads | solve (s) | solve speed-up | total speed-up |
|---|---|---|---|---|---|
| 568,152 | CH | 8 | 0.660 | 1.77 | 1.35 |
| 568,152 | CH-SP | 8 | 0.495 | **2.36** | **1.51** |
| 568,152 | CH-SP | 20 | 0.554 | 2.10 | 1.33 |
| 1,022,979 | CH | 20 | 1.246 | 1.60 | 1.21 |
| 1,022,979 | CH-SP | 8 | 1.276 | 1.56 | 1.13 |
| 1,022,979 | CH-SP | 20 | 0.969 | **2.06** | **1.34** |
| 1,867,768 | CH | 20 | 2.307 | 1.62 | 1.19 |
| 1,867,768 | CH-SP | 8 | 2.471 | 1.51 | 1.12 |
| 1,867,768 | CH-SP | 20 | 1.787 | **2.09** | **1.32** |
| 3,759,720 | CH | 8 | 6.287 | 1.06 | 0.91 |
| 3,759,720 | CH | 20 | 4.573 | 1.45 | 1.10 |
| 3,759,720 | CH-SP | 8 | 4.852 | 1.37 | 1.01 |
| 3,759,720 | CH-SP | 20 | 3.604 | **1.84** | **1.19** |

(CH with 8 threads at 1.0 M and 1.87 M: solve 1.19× and 1.18×, total 0.99× both.)

Iterations with threads equal the one-core Chebyshev counts (26–27). Solver memory: CH +8 %,
CH-SP +33 % against AMG-PCG (809 and 991 vs 747 MB at 3.76 M).

### Verdict

**AMG-PCG-CH-SP on 20 threads is the first candidate that beats AMG-PCG.** Its solve is 1.8–2.4×
faster from 0.57 M to 3.76 M unknowns. Setup is still serial and grew by about 1.5 s at 3.76 M
(coarsening plus the power iteration, 4.46 s vs 2.94 s), so setup plus solve is 1.2–1.5× faster.
On one core the V2 choice, Gauss–Seidel, stays best.

The V2 decision changes as follows. On a multicore machine, AMG-PCG with a Chebyshev smoother in
single precision is the fastest solver measured for HS-7 above about 0.5 M unknowns. Its cost is
a third more solver memory.

**Next.** Setup is now the larger share at 3.76 M (4.5 s setup vs 3.6 s solve). The next gains
are a parallel setup (power iteration and aggregation) and fewer power iterations, before
geometric multigrid (V3 step 2). Multi-load reuse (step 4) amortises setup in any case.

## V3C: parallel AMG setup

**Hypothesis.** After V3B, setup was the larger share at 3.76 M unknowns (4.5 s setup vs 3.6 s
solve) and did not use the threads at all. A profile of that run (new per-step timers in the
setup report) found: coarsening 2.95 s, of which prolongator 1.25 s (one sparse product), Galerkin
product 1.33 s (two sparse products), strength 0.22 s, aggregation 0.08 s, symmetrisation 0.05 s.
Then the Chebyshev eigenvalue estimate 0.96 s and the single-precision copy 0.23 s. Everything
except the greedy aggregation works row by row and can run in parallel.

**Implementation** (commit `93f65d6`). `sparse::multiply(pool, A, B)`: Gustavson's product with
its rows split over the pool's chunks and per-thread work arrays. Each row is computed exactly
as in the serial product, so the result is bit-identical. Strength, the filtered matrix (now
built in two passes, count then fill), the prolongator scaling, the Gershgorin bound, the power
iteration and the float copy also run on the pool. The power-iteration start vector is now a
hash of the row index, so V3C's λ̂ differs slightly from V3B's. Aggregation and the transposes
stay serial. Tests: `SparseOps.ParallelMultiplyIsBitIdenticalToSerial` and
`Amg.ParallelSetupBuildsTheSameHierarchyForAnyThreadCount` (every coarse operator, aggregate
and λ̂ identical on 1, 4 and 8 threads). 429 tests pass.

**A shortcut that failed.** With 10 instead of 20 power iterations the eigenvalue estimate saved
another 0.26 s. But λ̂ under-estimated λ_max(D⁻¹A), the preconditioner was no longer positive
definite, and CG stopped with Breakdown after 4 iterations at 3.76 M (true relative residual
3.1). The default stays at 20, and the option `amgChebyshevPowerIterations` records why.

**Runs.** HS-7 ladder k = 0…7: AMG-PCG on one pinned core (the V2 best) and AMG-PCG-CH-SP on 8
and 20 threads, all with the error observer. 32 runs, all Converged, commit `93f65d6`
(`results/v3/csv/V3C.csv`).

### Against V2's AMG-PCG on one core (same batch)

| DOF | AMG-PCG total (s) | CH-SP 8 thr total (s) | CH-SP 20 thr total (s) | best total speed-up | best solve speed-up | setup speed-up |
|---|---|---|---|---|---|---|
| 56,015 | 0.200 | 0.092 | 0.087 | 2.30 | 2.69 | 1.40–1.47 |
| 93,921 | 0.327 | 0.143 | 0.135 | 2.42 | 3.04 | 1.30–1.38 |
| 170,634 | 0.540 | 0.227 | 0.211 | 2.56 | 3.32 | 1.36–1.44 |
| 285,532 | 0.871 | 0.392 | 0.362 | 2.41 | 2.91 | 1.51–1.64 |
| 568,152 | 1.603 | 0.765 | 0.736 | 2.18 | 2.52 | 1.51–1.59 |
| 1,022,979 | 2.793 | 1.390 | 1.286 | 2.17 | 2.49 | 1.52–1.64 |
| 1,867,768 | 5.256 | 2.727 | 2.487 | 2.11 | 2.37 | 1.53–1.68 |
| 3,759,720 | 9.600 | 5.374 | 5.825 | 1.79 | 1.96 | 1.41–1.49 |

Total = setup + solve (median of 5 solves). The error against the reference is the same order
as AMG-PCG's at every size (e.g. 1.9e-11 vs 2.0e-11 K at 3.76 M). Iterations: 26–38 for CH-SP
against 23–50 for AMG-PCG. Solver memory is +33 % (991 vs 747 MB at 3.76 M).

**Run-to-run caution.** At 3.76 M on 8 threads the solve took 3.38 s here and 4.85 s in V3B. The
solve code is the same; only λ̂ (new start vector) and the setup changed. The difference is
larger than the min–max spread within either run (3.34–4.09 s here, 4.80–4.93 s in V3B), and
it is not explained yet. It needs a
repeat before any claim at that one point.

### Verdict

With a parallel setup, **AMG-PCG-CH-SP is faster than V2's best at every HS-7 size measured:
1.79–2.56× in setup plus solve on 8 or 20 threads**, with the same accuracy and a third more
solver memory. The serial parts left (aggregation, transposes, coarse factorisation) cap setup
at 1.3–1.7×. On one core, V2's Gauss–Seidel AMG-PCG is still the right choice. On this 20-thread
machine, 20 threads beat 8 except at 3.76 M.

## V3D: geometric multigrid (GMG-PCG, GMG-PCG-CH-SP)

**Hypothesis.** The survey found geometric multigrid (GMG) to be the strongest rival to AMG on
structured meshes: no strength or aggregation step, a cheap setup, and simple transfers. The
HS-7 mesh is a graded tensor grid with a cell mask, so every node has a grid index (i, j, k) and
a geometric hierarchy is available.

**Implementation** (commit `cea14c0`). `geometricProlongator` in `Amg.cpp`. Coarse nodes are
those whose original grid indices are multiples of 2^(l+1) on level l (every second grid line
per axis). A fine node interpolates linearly along the real grid lines from the corners of its
box, so linear fields are reproduced exactly on the graded grid. Corners that are holes in the
cell mask (gaps between fins) are dropped and the weights renormalised, which keeps constants.
A node with no corner left becomes coarse itself. Eliminated Dirichlet rows get no
interpolation. The Galerkin coarse operators, the smoothers (Gauss–Seidel or Chebyshev), the
single-precision cycle and the parallel setup are all reused from the AMG hierarchy. PCG now
passes the mesh hint (node coordinates and grid indices) to the preconditioner, and the study
runner supplies it. Tests: `Gmg.GeometricProlongatorKeepsConstantsAndLinearFields` (graded
lines, promotion, serial = parallel) and `Gmg.GeometricMultigridPcgConvergesAndNeedsTheHint`
(8 CG iterations at both 17³ and 33³ on the Q1 model problem; Breakdown, not a crash, without
the hint). 431 tests pass.

**Not tested here.** The survey's main argument for GMG is a *matrix-free* operator: no
assembled matrix, so less memory traffic. This GMG still assembles K and the Galerkin coarse
operators. A matrix-free version is a separate, larger step.

**Runs.** HS-7 ladder k = 0…7: AMG-PCG and GMG-PCG on one pinned core (Gauss–Seidel), and
AMG-PCG-CH-SP and GMG-PCG-CH-SP on 20 threads. Error observer on throughout. 40 runs, all
Converged, commit `cea14c0` (`results/v3/csv/V3D.csv`).

### Ratio AMG time / GMG time (> 1: GMG faster)

| DOF | 1 core total | 1 core setup | 1 core solve | 20 thr total | 20 thr setup | 20 thr solve | iterations AMG / GMG (1 core) |
|---|---|---|---|---|---|---|---|
| 56,015 | **1.54** | 1.75 | 1.49 | 0.85 | 1.05 | 0.78 | 50 / 34 |
| 93,921 | **1.34** | 1.81 | 1.25 | 1.07 | 1.49 | 0.92 | 42 / 32 |
| 170,634 | **1.22** | 1.85 | 1.11 | 0.98 | 1.44 | 0.81 | 35 / 31 |
| 285,532 | **1.11** | 1.82 | 0.97 | 0.89 | 1.30 | 0.74 | 31 / 31 |
| 568,152 | 0.88 | 1.72 | 0.75 | 0.83 | 1.31 | 0.68 | 28 / 37 |
| 1,022,979 | 0.91 | 1.67 | 0.77 | 1.01 | 1.51 | 0.84 | 26 / 33 |
| 1,867,768 | 0.91 | 1.69 | 0.77 | 0.96 | 1.26 | 0.85 | 26 / 33 |
| 3,759,720 | 0.77 | 1.69 | 0.62 | 0.90 | 1.25 | 0.79 | 23 / 36 |

GMG uses 4–7 levels at operator complexity 1.11–1.14, against AMG's 3–4 levels at 1.04–1.06.
Its V-cycle therefore does more work. Solver memory is within 6 % of AMG's at every size
(e.g. 723 vs 747 MB at 3.76 M on one core). Both stop at the same 1e-10 relative residual.
GMG's error against the reference equals AMG's up to 0.57 M; from 1 M up it is 2.0–4.6× larger,
with a largest value of 2.1e-10 K (AMG: at most 6.7e-11 K there). That is still far below any
engineering tolerance.

### Verdict

GMG does **not** replace AMG on HS-7. Its setup is consistently 1.7–1.9× cheaper on one core
and 1.25–1.5× on 20 threads. Its iterations stay at 31–37 on every mesh, while AMG's fall from
50 to 23 as the mesh is refined. Full geometric coarsening of HS-7's stretched cells (thin TIM
layer, fins) is weaker than coarsening that follows the strong couplings. So GMG wins only where
setup dominates:

- one core, up to 0.29 M unknowns: GMG-PCG is 1.11–1.54× faster than AMG-PCG;
- 20 threads: GMG-PCG-CH-SP is 7 % faster at 94 k and ties within 2 % at 0.17 M and 1.02 M;
  AMG-PCG-CH-SP is faster at the other five sizes, by up to 17 % (ratio 0.83 at 0.57 M).

The best solver from 0.3 M unknowns up stays AMG-PCG-CH-SP on threads (V3C). Two things could
change the GMG result: semi-coarsening or line smoothing across the thin layers (the usual cure
for anisotropy in GMG), and a matrix-free fine level. Both are larger than this step.

## V3E: several load cases, and pinned threads

**Machine state (checked 2026-11-27 before this batch).** Intel Core i5-14600K: 6 performance
cores (12 threads) and 8 efficiency cores, 20 logical CPUs. 2 × 16 GB DDR5 sold as 6000 MT/s
CL30 (AX5U6000C30) but running at 4800 MT/s (XMP off). Windows power plan Balanced with core
parking. Background load from other programs (Discord, a browser, Defender) was 4–6 % on
average during the V3 runs (column `cpu_load_pct`). Decisions (the user's): keep the RAM at
4800 MT/s so every V3 timing stays comparable; use the High performance plan during this batch
only; pin threads. Two notes on the power plan. The batch command meant to switch it back
afterwards did not run from the Bash tool, and this was caught and restored by hand. The plan
was active for V3E only, so V3A–V3D ran on Balanced.

**Pinning (harness change, commit `07b86c4`).** Multi-threaded runs left threads unpinned
until now, so serial work could land on an efficiency core. Now the caller is pinned to the
same CPU as the one-thread runs (CPU 11), helpers go first to one CPU on each other performance
core, then to the SMT siblings, then to the efficiency cores. Column `core` holds the caller's
CPU (11 in every V3E run).

**Implementation.** `LinearOperatorSolver::solveMany`: by default one solve per load after the
one setup (direct: one factorisation, L forward/back substitutions; PCG: one hierarchy, L
solves). `AmgBatchPcg` (AMG-PCG-CH-SP-B) runs L CG recurrences in lockstep. Block matrix
products and a block single-precision Chebyshev V-cycle store the L values of a row next to
each other, so K and every hierarchy level are read once per iteration for all L loads. Each
load keeps its own α, β and stopping test. Load 0 is the design load, so it is checked against
the reference. Loads 1…L−1 redistribute the same heat and convection loads with a smooth
factor 1 + 0.5·sin(2πj·x/Lx + 0.7j)·cos(πj·y/Ly), and the prescribed temperatures stay fixed.
Test `BatchPcg.LoadCasesInLockstepMatchSeparateSolves`: 6 loads, same iterations (16) as
separate solves, same answers to 1e-7, identical on 1 and 4 threads. 432 tests pass.

### Pinning check: AMG-PCG-CH-SP, one load, pinned (total = setup + solve, s)

| DOF | 6 threads | 8 threads | 12 threads | 20 threads | V3C (unpinned) 8 / 20 |
|---|---|---|---|---|---|
| 568,152 | **0.833** | 0.891 | 1.104 | 0.864 | 0.765 / 0.736 |
| 3,759,720 | 5.365 | 5.676 | **5.175** | 5.618 | 5.374 / 5.825 |

Pinned, 8 threads at 3.76 M solve in 3.57 s, close to V3C's 3.38 s. That makes V3B's 4.85 s
the outlier, consistent with (but not proof of) a bad thread placement there. The thread
counts differ by less than 10 % at 3.76 M. At 0.57 M the unpinned V3C runs were faster than any
pinned one, and 12 threads were the slowest. Pinning removed the outlier, but it did not make
the runs faster, and the best thread count is not stable between sizes. Six threads (one per
performance core) are within 4 % of the best pinned run at both sizes.

### Load cases (total = setup + all L solves, s; 12 pinned threads for CH-SP)

| DOF | L | V2 AMG-PCG, 1 core | DIRECT-ND, 1 core | CH-SP one by one | CH-SP-B lockstep | lockstep vs one by one | lockstep vs V2 |
|---|---|---|---|---|---|---|---|
| 93,921 | 1 | 0.343 | 10.616 | **0.159** | 0.209 | 0.76 | 1.64 |
| 93,921 | 5 | 1.371 | 10.590 | 0.484 | **0.378** | 1.28 | 3.63 |
| 93,921 | 20 | 5.189 | 11.174 | 1.804 | **0.937** | 1.93 | 5.54 |
| 285,532 | 1 | 0.880 | 72.878 | **0.364** | 0.461 | 0.79 | 1.91 |
| 285,532 | 5 | 3.421 | 75.017 | 1.176 | **0.851** | 1.38 | 4.02 |
| 285,532 | 20 | 12.837 | 76.838 | 4.360 | **2.099** | 2.08 | 6.12 |
| 1,022,979 | 1 | 2.786 | – | **1.337** | 1.525 | 0.88 | 1.83 |
| 1,022,979 | 5 | 10.866 | – | 4.706 | **2.930** | 1.61 | 3.71 |
| 1,022,979 | 20 | 40.933 | – | 16.959 | **7.121** | 2.38 | 5.75 |
| 3,759,720 | 1 | 9.739 | – | **5.175** | 5.670 | 0.91 | 1.72 |
| 3,759,720 | 5 | 37.234 | – | 18.103 | **11.003** | 1.65 | 3.38 |
| 3,759,720 | 20 | 141.444 | – | 65.606 | **25.976** | 2.53 | 5.45 |

(DIRECT-ND only up to 0.29 M: the factor of the larger meshes exceeds the 8 GB budget, as in V2.)

Every run converged. Iterations do not change with L (23 for AMG-PCG and 26 for CH-SP at
3.76 M). The error of load 0 against the reference is identical in lockstep and one by one. The
largest final relative residual over all loads is 9.6e-11.

**Memory is the price.** Lockstep at L = 20 holds 6,046 MB of solver memory at 3.76 M, against
991 MB one by one (6.1×). Each extra load adds about 6 double and 5 float vectors (about 260 MB
at 3.76 M). L = 32 is the coded upper limit, and the 8 GB budget would refuse it at 3.76 M.

**Direct solver.** With one factorisation reused, ND's extra cost per load at 94 k is 0.043 s
(0.871 s for 20 loads). That is about what lockstep CH-SP-B adds per load (0.038 s). But the
factorisation costs 10.3 s here and 73 s at 0.29 M, so ND never wins in this range.

### Verdict

- **For several loads, run them in lockstep.** AMG-PCG-CH-SP-B is 1.28–2.53× faster than the
  same solver one load at a time, the gain growing with L and with mesh size. It is 3.4–6.1×
  faster than V2's best solver (one-core AMG-PCG) for 5 and 20 loads, at the cost of up to 6×
  the solver memory at 20 loads.
- **For one load, use the plain AMG-PCG-CH-SP.** The block path costs 10–31 % more at L = 1.
- **Direct solvers do not pay off for many loads on HS-7 (as V2 found, H5); lockstep widens the gap.** The
  factorisation alone (10.3 s at 94 k, 73 s at 0.29 M) costs as much as about 220 and 700
  lockstep loads (0.047 and 0.105 s per load at L = 20).
- **Pinning removed the one outlier.** Six to twenty threads stay within 10 % of each other at
  3.76 M.

## V3F: external references (hypre BoomerAMG, PETSc GAMG)

**Question.** Is our AMG close to the state of the art, or do mature libraries do much better on
the same matrices?

**Libraries (downloaded with the user's OK, built locally, not vendored).**
- **hypre 3.2.1** (github.com/hypre-space/hypre, tag v3.2.1, tarball SHA-256 `3083cf94…4e0c`,
  MIT or Apache-2.0). Built with MSVC as a static library: no MPI, OpenMP (MSVC's OpenMP 2.0),
  static runtime. It is linked only with `-DUNIFORGE_WITH_HYPRE=ON -DHYPRE_ROOT=…`; the default
  build stays offline and the hypre test is skipped there. MSVC's OpenMP lacks
  `omp_get_thread_limit`, which hypre calls only to cap the thread count. A one-line shim
  returns `omp_get_max_threads()`.
- **PETSc 3.26.1** (web.cels.anl.gov release snapshot, SHA-256 `a4dc46c1…3496`, BSD-2-Clause),
  in WSL2 Ubuntu 24.04 (installed for this step, run as root, no user account). Built with GCC
  13.3 at `-O3 -march=native`, no MPI, sequential. PETSc's install check passed.

**How each is run.**
- **HYPRE-PCG / HYPRE-PCG-T05** (`src/core/solvers/external/HypreSolver.cpp`) run through our
  own harness, on the same process, matrices, tolerance and repetitions. K is copied into
  hypre's IJ/ParCSR format, and the copy is part of setup, reported as phase "copy to hypre".
  The solver is hypre PCG (two-norm, relative residual 1e-10) with one BoomerAMG V-cycle: HMIS
  coarsening, extended+i interpolation (≤ 4 per row), hybrid symmetric Gauss–Seidel, strength
  threshold 0.25 (hypre's default) or 0.5 (hypre's suggestion for 3D). The settings follow
  hypre's documentation and are not tuned for HS-7. One pinned core, and 6 threads where
  hypre's own OpenMP threads are not pinned. Commit `9af0828`, High performance power plan,
  56 runs (with AMG-PCG and AMG-PCG-CH-SP), all converged (`results/v3/csv/V3F.csv`).
- **PETSc CG + GAMG** (`tools/petsc_ref/petsc_ref.c`, `run_v3f_petsc.sh`). The same eight HS-7
  systems are exported with `uniforge_cli study export`, together with their references. PETSc
  CG uses the unpreconditioned norm, relative tolerance 1e-10 and x₀ = 0, so it stops on the
  same test. Setup (KSPSetUp, the GAMG hierarchy) and solve are timed separately: 5
  repetitions, medians. Two settings: GAMG defaults, and threshold 0.02 (our θ₀). One pinned
  virtual CPU. These timings come from a Linux VM on the same CPU, with Windows on the Balanced
  plan, so they are **indicative**, not exactly comparable with the Windows runs
  (`results/v3/csv/V3F_petsc.csv`).

### One core: total time (setup + solve, s)

| DOF | AMG-PCG (ours, V2) | hypre θ 0.25 | hypre θ 0.5 | PETSc GAMG default | PETSc GAMG thr 0.02 | best external / ours |
|---|---|---|---|---|---|---|
| 56,015 | **0.199** | 0.309 | 0.252 | 0.351 | 0.364 | 1.27 (hypre) |
| 93,921 | **0.331** | 0.531 | 0.464 | 0.640 | 0.658 | 1.40 (hypre) |
| 170,634 | **0.557** | 0.962 | 0.799 | 1.121 | 1.120 | 1.43 (hypre) |
| 285,532 | **0.917** | 1.733 | 1.716 | 1.841 | 1.807 | 1.87 (hypre) |
| 568,152 | **1.645** | 3.588 | 3.425 | 3.751 | 3.185 | 1.94 (PETSc) |
| 1,022,979 | **2.849** | 7.154 | 7.298 | 6.749 | 5.475 | 1.92 (PETSc) |
| 1,867,768 | **5.325** | 12.569 | 13.174 | 12.495 | 9.575 | 1.80 (PETSc) |
| 3,759,720 | **9.936** | 27.336 | 29.019 | 27.667 | 22.868 | 2.30 (PETSc) |

Iterations at 3.76 M: ours 23, hypre 17 and 22, PETSc 31 and 16. **Solve alone, our AMG-PCG and
the best external solver are close at large sizes**: 6.99 s ours, 6.92 s for PETSc GAMG thr
0.02 and 13.39 s for hypre at 3.76 M; 3.74 / 3.60 / 6.17 s at 1.87 M. hypre is the fastest
solve below 0.1 M (0.213 vs 0.261 s at 94 k). **The difference is the setup**: 2.94 s ours,
13.4 s PETSc and 13.9 s hypre at 3.76 M (hypre's copy of K is 1.1 s of that).

### 6 threads: total time (setup + solve, s)

| DOF | AMG-PCG-CH-SP (ours, V3) | hypre θ 0.25 | hypre θ 0.5 | best hypre / ours |
|---|---|---|---|---|
| 56,015 | **0.096** | 0.129 | 0.148 | 1.34 |
| 93,921 | **0.141** | 0.205 | 0.221 | 1.45 |
| 170,634 | **0.244** | 0.374 | 0.398 | 1.53 |
| 285,532 | **0.545** | 0.714 | 1.152 | 1.31 |
| 568,152 | **0.792** | 2.266 | 2.615 | 2.86 |
| 1,022,979 | **1.432** | 4.134 | 4.910 | 2.89 |
| 1,867,768 | **2.614** | 4.789 | 9.143 | 1.83 |
| 3,759,720 | **5.267** | 18.065 | 21.052 | 3.43 |

The PETSc build is sequential, so it has no 6-thread numbers. Peak process memory at 3.76 M is
3.0 GB for our runs and 4.6–4.9 GB for hypre's. Accuracy: every external run converged to the
same relative residual (true residual ≤ 9.9e-11). The largest error against the reference is
6.4e-10 K (hypre θ 0.5 at 1.87 M, where ours is 6.7e-11 K). PETSc's largest is 9.6e-11 K.

### Verdict

On HS-7 our AMG is **faster than both mature libraries in their standard settings, at every
size**. On one core it is 1.27–2.30× faster in total time than the best external result per
size (1.27–2.75× against hypre alone). On 6 threads it is 1.31–3.43× faster than hypre. The gain is in the
setup, not the solve. Per iteration and per solve, PETSc GAMG with threshold 0.02 matches our
AMG-PCG at the large sizes, and hypre is the best solve below 0.1 M. So our smoothed-aggregation
AMG is competitive with the state of the art. It is not ahead in convergence; it is ahead in
setup cost. That is a large advantage for one solve and a small one when a setup is reused for
many loads (V3E). Caveats: neither library was tuned for HS-7 (for example aggressive coarsening
or smaller interpolation stencils in hypre, setup reuse options in GAMG); hypre runs on MSVC's
older OpenMP without pinned threads; PETSc runs in a Linux VM. A tuned hypre or PETSc could
close part of the setup gap.
