#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/common/CancelToken.hpp"
#include "core/common/Progress.hpp"
#include "core/common/Result.hpp"
#include "core/perf/SystemInfo.hpp"

namespace uniforge::perf {

inline constexpr int kWarmupReps = 1;
inline constexpr int kTimedReps = 5;
inline constexpr double kAgreementTolerance_C = 1e-8; // brief D4: max |ΔT| ≤ 1e-8 °C
inline constexpr int kMaxBenchWorkers = 256;
// Two result sets (serial reference + the run being checked) must fit in this budget.
inline constexpr std::uint64_t kMaxResultBytes = 2ULL << 30;

struct BenchmarkConfig {
    std::uint64_t seed = 20261101;
    int cases = 10000;
    int elements = 8000;
    std::vector<int> workers{1, 2, 4}; // requested; clamped to the machine, see effectiveWorkers
};

// Presets (spec §7.4): Quick for the UI (well under a second per repetition), Report for the
// committed evidence: 10 000 x 8 000 (spec suggested 4 000 x 4 000, which measured T1 = 0.21 s
// on the i5-14600K; tuned so T1 >= 1 s; 640 MB per result set).
[[nodiscard]] BenchmarkConfig quickPreset();
[[nodiscard]] BenchmarkConfig reportPreset();

struct BenchRow {
    int workers = 1;  // p
    int threads = 1;  // threads actually started: min(p, cases); p = 1 uses the calling thread
    double warmupSec = 0.0;
    std::array<double, kTimedReps> repsSec{};
    double median = 0.0, min = 0.0, max = 0.0; // s
    double speedup = 0.0;                      // T1 / Tp (medians)
    double efficiency = 0.0;                   // speedup / p
};

struct BenchmarkReport {
    SystemInfo sys;
    BenchmarkConfig cfg;
    std::vector<int> effectiveWorkers;
    std::vector<BenchRow> rows; // one per effective p, ascending, p = 1 first
    double maxAbsDiff_C = 0.0;  // max |T_serial − T_run| over all nodes, cases and timed reps
    bool agreementPass = false; // maxAbsDiff_C ≤ kAgreementTolerance_C
    bool orderPreserved = false; // every out[i] carries the serial result of case i
    bool speedupAssessable = true; // false on a single-logical-CPU machine
    double resultSetBytes = 0.0;   // memory of one preallocated result set
    std::vector<std::string> notes;
};

// p values to run: each requested p clamped to [1, logicalCpus], duplicates removed, p = 1
// always included (it is the T1 baseline), ascending. On a machine with one logical CPU p = 2
// is added as a correctness demonstration (speedup not assessable). Every adjustment appends a
// sentence to `notes`.
[[nodiscard]] std::vector<int> effectiveWorkers(const std::vector<int>& requested,
                                                int logicalCpus, std::vector<std::string>& notes);

// Runs the Task D protocol (spec §7.4): generate the batch, preallocate two result sets, then
// for each effective p: 1 warm-up + 5 timed repetitions with steady_clock around the solve
// only. p = 1 is the serial runner, p ≥ 2 the parallel runner. Every repetition's output is
// compared with the serial warm-up result outside the timed region.
// Progress stages look like "Rep 3/5 · p = 2". Cancellation is checked per case.
// Errors: InvalidInput (bad config), LimitExceeded (result sets over kMaxResultBytes),
// Cancelled, Internal (worker failure).
[[nodiscard]] Result<BenchmarkReport> benchmark(const BenchmarkConfig& cfg,
                                                const ProgressFn& progress = nullptr,
                                                const CancelToken* cancel = nullptr);

// Same, with the machine description supplied (tests use it to simulate a 1- or 2-CPU machine).
[[nodiscard]] Result<BenchmarkReport> benchmark(const BenchmarkConfig& cfg,
                                                const SystemInfo& sys,
                                                const ProgressFn& progress = nullptr,
                                                const CancelToken* cancel = nullptr);

} // namespace uniforge::perf
