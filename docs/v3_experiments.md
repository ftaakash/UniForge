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
