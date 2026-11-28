# V2 report figures

Written by `uniforge_cli study figures` from `results/v2` (build spec §12.1). Every figure maps to the data it is drawn from. 3D renders and spy rasters are PNG files next to their SVG (`<figure>-<k>.png`).

| Figure | File | What it shows | Drawn from |
|---|---|---|---|
| P1 | [P1_hs7_render.svg](P1_hs7_render.svg) | HS-7 assembled and exploded (materials) | fem3d::makeHS7, mesh s = 1 mm |
| P2 | [P2_boundary_conditions.svg](P2_boundary_conditions.svg) | Boundary conditions and load | fem3d::makeHS7 face conditions |
| P3 | [P3_meshes.svg](P3_meshes.svg) | Meshes at s = 1.0 and 0.5 mm, TIM detail | fem3d::buildMesh(makeHS7, s) |
| P4 | [P4_temperature_field.svg](P4_temperature_field.svg) | Surface temperature field, two views | HS-7 at s = 0.5 mm, PCG-IC0 to 1e-11 (same system as E2-HS7-s0.500) |
| P5 | [P5_cut_planes.svg](P5_cut_planes.svg) | Cut planes through die and fin | HS-7 at s = 0.5 mm, fem3d::sampleTemperature |
| P5 | [P5_fin_profile.svg](P5_fin_profile.svg) | Middle fin: FE temperature vs 1-D fin theory | HS-7 at s = 0.5 mm, fin 4 centre line |
| P6 | [P6_v1_bar_profile.svg](P6_v1_bar_profile.svg) | Centre line of the 3D bar vs v1's exact parabola | E1-verify-bar |
| P6 | [P6_v1_bar_render.svg](P6_v1_bar_render.svg) | v1 bar in 3D | fem3d::makeV1Bar at s = 5 mm (as E1-verify-bar) |
| P7 | [P7_mms_convergence.svg](P7_mms_convergence.svg) | Manufactured solution: error vs cell size | E1-verify-mms4/8/16/32 |
| P8 | [P8_spy.svg](P8_spy.svg) | Sparsity of K and L (natural, RCM, ND) | HS-7 at s = 2 mm, solvers::DirectCholesky::factor() |
| P9 | [P9_amg_aggregates.svg](P9_amg_aggregates.svg) | AMG aggregates and hierarchy | HS-7 at s = 1 mm, solvers::AmgHierarchy with the default options |
| P10 | [P10_residual_history.svg](P10_residual_history.svg) | Convergence on HS-7 at s = 0.5 mm (286 k DOF) | E2-HS7-s0.500-{PCG-J,PCG-IC0,AMG,AMG-PCG} |
| P11 | [P11_time_vs_dof.svg](P11_time_vs_dof.svg) | Time to solve HS-7 once vs problem size (setup + solve) | results/v2/csv/E2.csv |
| P11 | [P11b_setup_vs_dof.svg](P11b_setup_vs_dof.svg) | Setup time vs problem size | results/v2/csv/E2.csv |
| P11 | [P11c_solve_vs_dof.svg](P11c_solve_vs_dof.svg) | Solve time vs problem size (one load case) | results/v2/csv/E2.csv |
| P12 | [P12_memory_vs_dof.svg](P12_memory_vs_dof.svg) | Peak memory vs problem size | results/v2/csv/E2.csv |
| P13 | [P13a_iterations_vs_dof.svg](P13a_iterations_vs_dof.svg) | Iterations vs problem size (HS-7) | results/v2/csv/E2.csv |
| P13 | [P13b_iterations_vs_h.svg](P13b_iterations_vs_h.svg) | Iterations vs film coefficient h (HS-7, 286 k DOF) | results/v2/csv/E3.csv |
| P13 | [P13c_iterations_vs_ktim.svg](P13c_iterations_vs_ktim.svg) | Iterations vs TIM conductivity (contrast to aluminium 200 W/(m·K)) | results/v2/csv/E3.csv |
| P14 | [P14_setup_vs_solve.svg](P14_setup_vs_solve.svg) | Setup vs solve for 1 and 20 load cases (HS-7, 171 k DOF) | results/v2/csv/E5_load_cases.csv |
| P15 | [P15_complexity_ladder.svg](P15_complexity_ladder.svg) | Complexity ladder with winner and ratio | results/v2/csv/E4.csv; thumbnails at s = 2 mm (bar 5 mm) |
| P16 | [P16_decision_chart.svg](P16_decision_chart.svg) | Decision chart: which solver when | results/v2/csv/E5_load_cases.csv, E2.csv, E6.csv |
| P17 | [P17_architecture.svg](P17_architecture.svg) | Software architecture | src/core/*, build spec §4 |
| P18 | [P18_algorithm_flow.svg](P18_algorithm_flow.svg) | Algorithm flow per solver | src/core/solvers/{direct,krylov,amg}, build spec §5 |
| P19 | [P19_error_vs_time.svg](P19_error_vs_time.svg) | Error against the reference vs time (HS-7, 568 k DOF) | E2-HS7-s0.397-*; E2_time_to_accuracy.csv |
| P20 | [P20_iterations_vs_sqrt_kappa.svg](P20_iterations_vs_sqrt_kappa.svg) | Iterations vs √κ estimate (E2 sizes and E3 conditioning sweep) | results/v2/csv/E2.csv, E3.csv |
| P21 | [P21_failure_modes.svg](P21_failure_modes.svg) | Failure-mode table and E8 outcomes | results/v2/csv/E8.csv, all_runs.csv |
| P22 | [P22_bar_vs_cube.svg](P22_bar_vs_cube.svg) | Same physics, two simple objects: v1 bar (10 k DOF) vs cube (16 k DOF) | results/v2/csv/E0.csv |
| P23 | [P23_thread_scaling.svg](P23_thread_scaling.svg) | All cores: time vs threads (HS-7, 568 k DOF, E6) | results/v2/csv/E6.csv |
| P24 | [P24_amg_threshold_fix.svg](P24_amg_threshold_fix.svg) | AMG strength threshold: before (θ₀ = 0.08) and after (θ₀ = 0.02), HS-7 | results/v2_superseded/amg_theta008/compare_theta008_vs_002.csv |
