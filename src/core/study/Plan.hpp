#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/common/Result.hpp"
#include "core/fem3d/Design.hpp"
#include "core/solvers/SolverTypes.hpp"

namespace uniforge::study {

// One experiment run (build spec §6.3, §8.2). Runs are defined in code (planExperiments), so a
// child process re-creates its RunSpec from the id alone (findRun) and no manifest has to be
// parsed; the manifest file is written for the record.

enum class DesignKey { V1Bar, Cube, Plate, FinnedPlate, HS7, PinFin };
enum class RunRole {
    Solve,        // time a solver on a system
    Reference,    // compute T_ref for a system (direct ND within budget, else AMG-PCG to 1e-12)
    Verification, // E1 checks (bar, manufactured solution, fin)
};

struct RunSpec {
    std::string id;         // unique, file-name safe, e.g. "E2-HS7-s0.500-PCG-J"
    std::string experiment; // "E0" … "E8", "T0" (CI smoke test)
    RunRole role = RunRole::Solve;
    std::string check; // Verification: "bar", "mms4", "fin", …

    // The linear system
    DesignKey design = DesignKey::HS7;
    double spacing = 1e-3; // m
    int timCells = 1;
    double h = 25.0;            // W/(m²·K), all convective faces
    double kTim = 3.0;          // W/(m·K)
    double timThickness = 1e-4; // m
    bool singular = false;      // E8: every face insulated (K singular)

    // The solver
    solvers::SolverKind solver = solvers::SolverKind::PcgJacobi;
    bool naturalOrdering = false; // E7: DIRECT with the natural ordering
    bool ic0NoShift = false;      // E8: IC(0) without the shift ladder
    bool observer = true;         // track the error against T_ref on the first repetition
    int threads = 1;              // 1 = pinned to one performance core (amendment A8)

    // Caps (build spec §7, D-17)
    double relTol = 1e-10;
    int maxIterations = 10000;
    double wallTimeCap = 1800.0;                     // s
    std::size_t memoryBudget = std::size_t(8) << 30; // solver bytes
};

// Short design name used in ids and tables: "bar", "cube", "plate", "fins", "HS7", "pinfin".
[[nodiscard]] std::string designName(DesignKey key);

// The design of a run, with its parameters applied (h, TIM, singular).
[[nodiscard]] fem3d::Design makeDesign(const RunSpec& spec);

// Identifies the linear system (design, parameters, spacing, TIM cells): runs with the same key
// share one reference solution.
[[nodiscard]] std::string systemKey(const RunSpec& spec);

// The runs of the given experiments ("E0" … "E8", "T0"; empty = all of E0–E8), in the order
// they should run: short experiments first, E2's large meshes last (amendment A6), each
// system's reference before its solver runs. dryRun keeps the two smallest sizes of every
// sweep, one solver variant per family where sizes grow, and tighter caps.
[[nodiscard]] std::vector<RunSpec> planExperiments(const std::vector<std::string>& experiments,
                                                   bool dryRun);

// The run with this id in the full plan (or the dry-run plan when dryRun). InvalidInput if absent.
[[nodiscard]] Result<RunSpec> findRun(const std::string& id, bool dryRun);

// HS-7 spacing ladder s_k = 1 mm · 2^(−k/3), k = 0 … 7 (amendment A1).
[[nodiscard]] double ladderSpacing(int k);

} // namespace uniforge::study
