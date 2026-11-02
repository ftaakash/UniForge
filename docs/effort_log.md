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
