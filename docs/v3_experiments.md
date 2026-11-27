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
