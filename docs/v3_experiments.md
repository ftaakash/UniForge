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
