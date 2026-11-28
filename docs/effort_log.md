# Effort Log

The brief asks for **actual** effort to be recorded (target 40–50 h). Planned hours come from the
build specification §13. Actual hours are wall-clock time taken from the timestamps of the
working-session logs (IST, UTC+5:30), from the first prompt of a phase to its last commit or
review. Breaks longer than 25 minutes, including two usage-limit pauses, are not counted.
Phases 4–7 ran at the same time in four parallel sessions, so their shared 4.4 h is counted once.

| Date | Phase | Work | Planned h | Actual h | Window (IST) |
|---|---|---|---:|---:|---|
| 2026-11-01 | 0 Setup | Brief analysis, three design passes, merged build spec, toolchain install, repo skeleton | 2 | 1.0 | 18:00–18:55 |
| 2026-11-01 | 1 Skeleton + packaging | Vendored deps, Win32/D3D11 + WARP, theme/fonts/shell, JobRunner + tests, launcher, packaging, CI; review; Sandbox test | 6 | 1.9 | 18:55–20:54 |
| 2026-11-01 | 2 Thermal FEA | Result/Error, CSV/SVG/JSON writers, rod solver (assembly, symmetric BCs, Thomas), error analysis, convergence study, CLI + golden test, evidence, thermal_fea.md; review | 6 | 2.7 | 20:28–23:10 |
| 2026-11-01 | 3 Importers | Line reader, STL and CSV parsers with validation, 37 negative fixtures + unit tests, gen-samples / inspect / import-bench, sample data + PROVENANCE.md, measured limits; review | 5 | 0.8 | 23:11–23:56 |
| 2026-11-02 | 4 Visual views | Render module (camera, picking, mesh + point renderers, shaders), Geometry and Thermal views, widgets, Format, Overview live status, 34 tests, docs | 7 | 4.4 (parallel, shared with 5–7) | 23:52–04:18 |
| 2026-11-02 | 5 Multicore | Rng + batch generator, worker pool and runners, benchmark harness + system info, `bench` CLI, Performance view, 39 tests, Linux sanitizer CI, provisional report, performance.md | 5 | (in the 4.4 above) | 23:52–04:12 |
| 2026-11-02 | 6 Embedded | Datasheet research (UM1724, DS10693, Basys 3 RM, SEGGER), design doc + block diagram, core/hw mocks + retry policy, DeviceManager + Hardware view, 23 tests | 4 | (in the 4.4 above) | 23:52–04:08 |
| 2026-11-02 | 7 Backprop | core/ml (forward, backward, update, FD check, trainer), value/FD/update/loss tests, CLI backprop + golden test, Backprop view, backprop.md, appendix checklist; handwritten appendix | 2.5 | (in the 4.4 above) | 23:52–04:03 |
| 2026-11-02 | Integration | Merge of Phases 4–7, official benchmark, manual test of the package, three UI fixes, Candidate ID, CHANGELOG/AI-log order; review | – | 1.5 | 04:18–05:20, 07:48–08:15 |
| 2026-11-02 | 8 Hardening (trimmed) | Crash handler, edge-case runs (Unicode path, read-only folder, WARP, crash), Overview live status | 3.5 | 0.3 | 08:15–08:30 |
| 2026-11-02 | 9 Delivery | Screenshots, report (5 pages), ownership statement, walkthrough, demo script, README, git bundle, final package, demo recording | 5 | 1.1 | 08:30–09:05, 09:10–09:35 |

**Running total:** 13.7 h actual, against 46 h planned.

The difference comes mainly from AI-assisted code generation (see `ai_usage_log.md`) and from
running Phases 4–7 in parallel. Not included: reading the brief before the first session, and
writing the handwritten appendix by hand.

## V2 (Assignment Extension Notice)

Planned hours come from the V2 build specification §13 (`docs/design/v2/UniForge_V2_Build_Spec.md`).
Actual hours use the same method as v1: wall-clock time from the timestamps of the working-session
logs (IST, UTC+5:30), with gaps longer than 25 minutes removed. V2 ran in one build session and one
planning/review session; where they overlap the time is counted once. Phases inside one sitting are
split at their commit times. Machine time without a session (the official batch, 2 h 46 min, and
the AMG re-runs, about 25 min) is not counted.

| Date | Phase | Work | Planned h | Actual h | Window (IST) |
|---|---|---|---:|---:|---|
| 2026-11-15 | V2-0 Design | Notice analysis, plan, design review, system design, merged build spec, figure tokens, branch | 1–2 | 0.90 | 17:40–18:34 |
| 2026-11-18 | V2-1 Sparse kernels | CSR storage, persistent kernel pool with deterministic reductions, vector kernels, SpMV, transpose, SpGEMM, 29 tests | 3–4 | 0.17 | 22:36–22:46 |
| 2026-11-18 | V2-2 Geometry and mesher | Box designs L0a–L4, graded tensor-grid hex mesher, boundary faces, surface export, 19 tests, measured mesh sizes | 2–3 | 0.32 | 22:46–23:05 |
| 2026-11-21 | V2-3 Solver interface and PCG-J | Solver types and reports, two-phase interface, observers, κ estimate, Jacobi-PCG, module-rule lint, 15 tests | 2–3 | 0.15 | 22:16–22:25 |
| 2026-11-21 | V2-4 3D FEA and verification | Q1 element, CSR assembly, convection and Dirichlet, energy audit, post-processing, verification ladder, fault injection, 18 tests | 3–4 | 0.23 | 22:25–22:39 |
| 2026-11-21 | V2-5 Direct Cholesky | RCM and nested-dissection orderings, symbolic with budget, up-looking Cholesky, ladder measurement, 11 tests; main README note; Linux CI fix next day | 4–6 | 0.33 | 22:39–22:58, 11-22 17:42–17:43 |
| 2026-11-22 | V2-6 IC(0) | IC(0) with shift ladder, PCG-IC0, positive-coupling element test, 7 tests | 1 | 0.08 | 17:43–17:48 |
| 2026-11-22 | V2-7 AMG | Strength, aggregation, smoothed prolongator, Galerkin hierarchy, symmetric V-cycle, AMG and AMG-PCG, diagnostics, 8 tests | 4–6 | 0.22 | 17:48–18:01 |
| 2026-11-22/23 | V2-8 Harness and official batch | Study plan, runner, collect, study CLI, Job Object / pinning / keep-awake, dry run of 85 runs, direct singularity fix, 10 tests; official batch of 241 runs (machine time not counted), first reading of the hypotheses, batch-log fix | 7–9 | 1.02 | 18:01–18:33, 11-23 04:14–04:36¹, 07:23–07:30¹ |
| 2026-11-23 | V2-9 Figures | `core/viz` (rasteriser, spy, colormaps, SVG canvas, PNG writer moved and compressed), SvgPlot extras, `study figures` P1–P22, AMG candidate check (72 runs), 16 tests, Linux test fix | 7–9 | 0.63 | 07:30–08:08¹ |
| 2026-11-23 | V2-9.1 Fixes | Renders as PNG files (hygiene scan), AMG θ₀ 0.08 → 0.02 with Q1 test, official re-run of 72 AMG rows, before/after and hypotheses re-read, figure text | 2–3 | 0.63 | 13:02–13:40 |
| 2026-11-23 | V2-10 Report and release | Report (22 pages), number filler with provenance, PDF check, reference and colour verification, marker resolution, added sections, review fixes, figures P23–P24 and readable plots, release `v2.0.0`, main README | 5–7 | 1.37 | 13:40–15:02 |

¹ Run unattended while the user slept, by the user's instruction to continue after the batch;
reviewed afterwards.

**Running total:** 6.05 h actual for V2, against 41–57 h planned (§13 without machine time).
Together with v1: 19.8 h.

The difference to the plan comes mainly from AI-assisted code generation (see `ai_usage_log.md`);
the experiments themselves ran as unattended machine time. Not included: reading the notice
before the first session, and reading the final report.

### Design decisions made by the user

The assistant proposed options with their trade-offs; these decisions were the user's, and they
shaped what V2 is:

| When | Decision |
|---|---|
| V2-0 | Fold all fifteen design-review amendments (A1–A15) into the plan instead of treating them as optional, which set the fairness and evidence standards of the study (fresh process per run, observer subtraction, generated report numbers, failure taxonomy) |
| V2-0 / V2-1 | Take the recommended defaults: the HS-7 heat-sink object with its complexity ladder, the direct / PCG / AMG solver triple with six variants, all solvers written from scratch, Q1 hexahedra, and the stronger architecture (Design B) over the simpler one |
| V2-1 | Continue without waiting for a deadline; no Eigen download (every number comes from the study's own code); no GUI extension; one commit per phase with a tag |
| V2-8 | Run the official batch overnight on an idle machine and continue the next phase unattended |
| V2-9 | Keep the packaging hygiene scan strict and change the figures instead (renders as separate PNG files) |
| V2-9 | Treat the AMG threshold as a bug: adopt θ₀ = 0.02, re-run the 72 AMG rows officially, and document the flaw and its fix with before and after numbers rather than report the weak AMG results |
| V2-10 | Keep the report at 18–25 pages by adding real content (accuracy, scaling, time to an engineering answer, orderings, hypotheses) rather than lowering the limit |
| V2-10 | After reviewing the draft: correct five statements, add the thread-scaling and AMG before/after figures, make every plot readable in print, and remove the half-empty pages |
| Release | Tag the release only after CI passed on Windows and Linux |
