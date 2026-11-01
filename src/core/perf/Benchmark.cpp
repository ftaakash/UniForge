#include "core/perf/Benchmark.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
#include <string>
#include <utility>

#include "core/perf/BatchGenerator.hpp"
#include "core/perf/ParallelRunner.hpp"
#include "core/perf/SerialRunner.hpp"
#include "core/perf/WorkerPool.hpp"

namespace uniforge::perf {

namespace {

std::string fmt(const char* format, double v) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, format, v);
    return buffer;
}

std::string joinInts(const std::vector<int>& values) {
    std::string s;
    for (const int v : values) {
        s += (s.empty() ? "" : ", ") + std::to_string(v);
    }
    return s;
}

// Overwrites every value so a repetition that skipped a case cannot pass the agreement check
// with the previous repetition's numbers.
void poison(std::vector<CaseResult>& results) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (CaseResult& r : results) {
        std::fill(r.T.begin(), r.T.end(), nan);
        r.Tmax = nan;
        r.xAtTmax = nan;
    }
}

struct Comparison {
    double maxAbsDiff = 0.0;
    bool orderPreserved = true;
};

// max |ref − run| over every node of every case (NaN counts as infinitely different), and
// whether each case's hottest node sits where the serial run put it. Cases differ in k and q,
// so a result written to the wrong index changes Tmax.
Comparison compare(const std::vector<CaseResult>& ref, const std::vector<CaseResult>& run) {
    Comparison c;
    for (std::size_t i = 0; i < ref.size(); ++i) {
        const std::vector<double>& a = ref[i].T;
        const std::vector<double>& b = run[i].T;
        if (a.size() != b.size()) {
            c.maxAbsDiff = std::numeric_limits<double>::infinity();
            c.orderPreserved = false;
            continue;
        }
        for (std::size_t j = 0; j < a.size(); ++j) {
            double d = std::fabs(a[j] - b[j]);
            if (std::isnan(d)) {
                d = std::numeric_limits<double>::infinity();
            }
            c.maxAbsDiff = std::max(c.maxAbsDiff, d);
        }
        if (!(run[i].Tmax == ref[i].Tmax && run[i].xAtTmax == ref[i].xAtTmax)) {
            c.orderPreserved = false;
        }
    }
    return c;
}

Result<void> validate(const BenchmarkConfig& cfg) {
    if (cfg.workers.empty()) {
        return makeError(ErrorCode::InvalidInput, "No worker counts were given.",
                         "Give at least one worker count, e.g. 1,2,4.");
    }
    for (const int p : cfg.workers) {
        if (p < 1 || p > kMaxBenchWorkers) {
            return makeError(ErrorCode::InvalidInput,
                             "Worker count " + std::to_string(p) + " is outside 1.." +
                                 std::to_string(kMaxBenchWorkers) + ".",
                             "Use worker counts between 1 and " +
                                 std::to_string(kMaxBenchWorkers) + ".");
        }
    }
    return {};
}

} // namespace

BenchmarkConfig quickPreset() {
    BenchmarkConfig cfg;
    cfg.cases = 500;
    cfg.elements = 1000;
    return cfg;
}

BenchmarkConfig reportPreset() {
    return BenchmarkConfig{}; // 10000 cases x 8000 elements, seed 20261101, p = 1, 2, 4
}

std::vector<int> effectiveWorkers(const std::vector<int>& requested, int logicalCpus,
                                  std::vector<std::string>& notes) {
    const int logical = std::max(1, logicalCpus);
    std::set<int> chosen{1};
    std::vector<int> capped;
    for (const int p : requested) {
        const int clamped = std::clamp(p, 1, logical);
        if (clamped != p) {
            capped.push_back(p);
        }
        chosen.insert(clamped);
    }
    if (!capped.empty()) {
        notes.push_back("Requested p = " + joinInts(capped) + " exceeds the " +
                        std::to_string(logical) + " logical CPU(s) of this machine and was "
                        "capped at " + std::to_string(logical) + ".");
    }
    if (std::find(requested.begin(), requested.end(), 1) == requested.end()) {
        notes.push_back("p = 1 was added: it is the serial baseline T1 for speedup.");
    }
    if (logical == 1) {
        chosen.insert(2);
        notes.push_back("Only 1 logical CPU: p = 2 was run as a correctness demonstration "
                        "(agreement and order are still checked). Speedup is not assessable on "
                        "this machine.");
    }
    return {chosen.begin(), chosen.end()};
}

Result<BenchmarkReport> benchmark(const BenchmarkConfig& cfg, const ProgressFn& progress,
                                  const CancelToken* cancel) {
    return benchmark(cfg, querySystemInfo(), progress, cancel);
}

Result<BenchmarkReport> benchmark(const BenchmarkConfig& cfg, const SystemInfo& sys,
                                  const ProgressFn& progress, const CancelToken* cancel) {
    using Clock = std::chrono::steady_clock;
    const auto report = [&](double fraction, const std::string& stage) {
        if (progress) {
            progress(fraction, stage);
        }
    };

    if (auto valid = validate(cfg); !valid) {
        return std::move(valid).error();
    }
    const double setBytes = static_cast<double>(cfg.cases) *
                            (static_cast<double>(cfg.elements) + 1.0) * sizeof(double);
    if (cfg.cases > 0 && cfg.elements > 0 &&
        2.0 * setBytes > static_cast<double>(kMaxResultBytes)) {
        return makeError(ErrorCode::LimitExceeded,
                         "cases x (elements + 1) needs " + fmt("%.0f", 2.0 * setBytes / 1e6) +
                             " MB for the two result sets, over the " +
                             fmt("%.0f", static_cast<double>(kMaxResultBytes) / 1e6) +
                             " MB limit.",
                         "Reduce the number of cases or elements.");
    }

    BenchmarkReport out;
    out.sys = sys;
    out.cfg = cfg;
    out.resultSetBytes = setBytes;
    out.effectiveWorkers = effectiveWorkers(cfg.workers, sys.logicalCpus, out.notes);
    out.speedupAssessable = sys.logicalCpus > 1;

    // --- Outside the timed region: workload and result buffers ---------------------------
    report(0.0, "Generating batch");
    auto batch = generateBatch(cfg.seed, cfg.cases, cfg.elements);
    if (!batch) {
        return std::move(batch).error();
    }
    const std::vector<thermal::RodProblem>& cases = batch.value();
    std::vector<CaseResult> reference;
    std::vector<CaseResult> run;
    prepareResults(cases, reference);
    prepareResults(cases, run);

    const auto& ps = out.effectiveWorkers;
    const double totalSteps = static_cast<double>(ps.size() * (kWarmupReps + kTimedReps));
    double step = 0.0;
    bool haveReference = false;
    Comparison worst;

    for (const int p : ps) {
        BenchRow row;
        row.workers = p;
        row.threads = p == 1 ? 1 : WorkerPool(p).threadsFor(cases.size());
        for (int rep = -kWarmupReps; rep < kTimedReps; ++rep) {
            const std::string pText = " · p = " + std::to_string(p);
            report(step / totalSteps, rep < 0 ? "Warm-up" + pText
                                              : "Rep " + std::to_string(rep + 1) + "/" +
                                                    std::to_string(kTimedReps) + pText);
            // The serial warm-up writes the reference; everything else writes `run`.
            const bool isReference = !haveReference;
            std::vector<CaseResult>& target = isReference ? reference : run;
            poison(target);

            const Clock::time_point t0 = Clock::now();
            const Result<void> solved = p == 1 ? runSerialInto(cases, target, cancel)
                                               : runParallelInto(cases, p, target, cancel);
            const Clock::time_point t1 = Clock::now();
            if (!solved) {
                return solved.error();
            }
            const double seconds = std::chrono::duration<double>(t1 - t0).count();

            if (isReference) {
                haveReference = true;
            } else {
                const Comparison c = compare(reference, run);
                worst.maxAbsDiff = std::max(worst.maxAbsDiff, c.maxAbsDiff);
                worst.orderPreserved = worst.orderPreserved && c.orderPreserved;
            }
            if (rep < 0) {
                row.warmupSec = seconds;
            } else {
                row.repsSec[static_cast<std::size_t>(rep)] = seconds;
            }
            step += 1.0;
        }
        std::array<double, kTimedReps> sorted = row.repsSec;
        std::sort(sorted.begin(), sorted.end());
        row.min = sorted.front();
        row.max = sorted.back();
        row.median = sorted[kTimedReps / 2];
        out.rows.push_back(row);
    }

    const double t1 = out.rows.front().median;
    for (BenchRow& row : out.rows) {
        row.speedup = row.median > 0.0 ? t1 / row.median : 0.0;
        row.efficiency = row.speedup / row.workers;
    }
    out.maxAbsDiff_C = worst.maxAbsDiff;
    out.agreementPass = worst.maxAbsDiff <= kAgreementTolerance_C;
    out.orderPreserved = worst.orderPreserved;

    // --- Method notes, written into every report ------------------------------------------
    out.notes.push_back(
        "Timed region: the batch solve only (steady_clock). Batch generation, the two "
        "preallocated result sets (" +
        fmt("%.1f", setBytes / 1e6) +
        " MB each), the NaN pre-fill and the agreement check run outside it.");
    out.notes.push_back(
        "p = 1 is the serial runner on the calling thread. p >= 2 start p std::threads per "
        "repetition (thread start-up and join are inside the timed region) that take chunks of "
        "max(1, cases / (8p)) cases from an atomic index.");
    out.notes.push_back("Agreement: every timed repetition (serial and parallel) is compared "
                        "node by node with the serial warm-up result.");
    if (std::any_of(ps.begin(), ps.end(), [](int p) { return p > 4; })) {
        out.notes.push_back("p > 4 is beyond the requirement (p = 1, 2, <= 4) and shown for "
                            "context only.");
    }
    for (const BenchRow& row : out.rows) {
        if (row.workers > 1 && row.threads < row.workers) {
            out.notes.push_back("p = " + std::to_string(row.workers) + " exceeds the case count; "
                                "only " + std::to_string(row.threads) + " threads were started.");
        }
    }
#if !defined(NDEBUG)
    out.notes.push_back("Debug build: timings are not representative; use a Release build.");
#endif
    report(1.0, "Done");
    return out;
}

} // namespace uniforge::perf
