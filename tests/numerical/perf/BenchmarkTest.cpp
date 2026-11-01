// Benchmark protocol (spec §5.2, §7.4, D3/D4): worker cap, warm-up + 5 reps, agreement,
// single-CPU disclosure, cancellation, report files.

#include "core/perf/Benchmark.hpp"
#include "core/perf/BenchmarkReportIO.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using uniforge::CancelToken;
using uniforge::ErrorCode;
using namespace uniforge::perf;

namespace {

SystemInfo fakeMachine(int logical) {
    SystemInfo s;
    s.cpu = "Test CPU";
    s.logicalCpus = logical;
    s.physicalCores = logical;
    return s;
}

BenchmarkConfig tiny() {
    BenchmarkConfig cfg;
    cfg.cases = 40;
    cfg.elements = 30;
    return cfg;
}

bool mentions(const std::vector<std::string>& notes, const std::string& text) {
    return std::any_of(notes.begin(), notes.end(),
                       [&](const std::string& n) { return n.find(text) != std::string::npos; });
}

} // namespace

TEST(Benchmark, WorkerCapOnTwoCpuMachine) {
    std::vector<std::string> notes;
    EXPECT_EQ(effectiveWorkers({1, 2, 4}, 2, notes), (std::vector<int>{1, 2}));
    EXPECT_TRUE(mentions(notes, "capped at 2"));
}

TEST(Benchmark, NoCapOnLargeMachine) {
    std::vector<std::string> notes;
    EXPECT_EQ(effectiveWorkers({4, 2, 1, 2}, 20, notes), (std::vector<int>{1, 2, 4}));
    EXPECT_TRUE(notes.empty());
}

TEST(Benchmark, SerialBaselineAlwaysIncluded) {
    std::vector<std::string> notes;
    EXPECT_EQ(effectiveWorkers({4}, 8, notes), (std::vector<int>{1, 4}));
    EXPECT_TRUE(mentions(notes, "p = 1 was added"));
}

TEST(Benchmark, SingleCpuForcesDemonstrationAndDisclosure) {
    std::vector<std::string> notes;
    EXPECT_EQ(effectiveWorkers({1, 2, 4}, 1, notes), (std::vector<int>{1, 2}));
    EXPECT_TRUE(mentions(notes, "not assessable"));

    const auto r = benchmark(tiny(), fakeMachine(1));
    ASSERT_TRUE(r.ok());
    EXPECT_FALSE(r.value().speedupAssessable);
    EXPECT_TRUE(mentions(r.value().notes, "Only 1 logical CPU"));
    const std::string md = benchmarkMarkdown(r.value(), {});
    EXPECT_NE(md.find("n/a"), std::string::npos);
}

TEST(Benchmark, ProtocolWarmupPlusFiveRepsAndAgreement) {
    std::vector<std::string> stages;
    std::mutex m;
    const auto r = benchmark(tiny(), fakeMachine(4), [&](double, std::string_view stage) {
        const std::lock_guard<std::mutex> lock(m);
        stages.emplace_back(stage);
    });
    ASSERT_TRUE(r.ok());
    const BenchmarkReport& rep = r.value();
    EXPECT_EQ(rep.effectiveWorkers, (std::vector<int>{1, 2, 4}));
    ASSERT_EQ(rep.rows.size(), 3U);
    for (const BenchRow& row : rep.rows) {
        EXPECT_GT(row.warmupSec, 0.0);
        for (const double s : row.repsSec) {
            EXPECT_GT(s, 0.0);
        }
        EXPECT_LE(row.min, row.median);
        EXPECT_LE(row.median, row.max);
        EXPECT_DOUBLE_EQ(row.efficiency, row.speedup / row.workers);
    }
    EXPECT_DOUBLE_EQ(rep.rows[0].speedup, 1.0);
    EXPECT_EQ(rep.maxAbsDiff_C, 0.0); // bitwise equal, not just within 1e-8
    EXPECT_TRUE(rep.agreementPass);
    EXPECT_TRUE(rep.orderPreserved);
    EXPECT_TRUE(rep.speedupAssessable);

    // 3 values of p x (1 warm-up + 5 reps) + "Generating batch" + "Done".
    const auto count = [&](const std::string& s) {
        return std::count(stages.begin(), stages.end(), s);
    };
    EXPECT_EQ(count("Warm-up · p = 2"), 1);
    EXPECT_EQ(count("Rep 3/5 · p = 2"), 1);
    EXPECT_EQ(count("Rep 5/5 · p = 4"), 1);
    EXPECT_EQ(count("Rep 6/5 · p = 4"), 0);
    EXPECT_EQ(stages.size(), 3U * 6U + 2U);
}

TEST(Benchmark, CancelReturnsCancelledAndNoReport) {
    CancelToken cancel;
    int calls = 0;
    const auto r = benchmark(tiny(), fakeMachine(4), [&](double, std::string_view stage) {
        if (++calls == 4 && stage.find("Rep") != std::string_view::npos) {
            cancel.requestCancel();
        }
    }, &cancel);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::Cancelled);
}

TEST(Benchmark, RejectsBadConfig) {
    BenchmarkConfig cfg = tiny();
    cfg.workers = {};
    EXPECT_EQ(benchmark(cfg, fakeMachine(4)).error().code, ErrorCode::InvalidInput);
    cfg.workers = {0};
    EXPECT_EQ(benchmark(cfg, fakeMachine(4)).error().code, ErrorCode::InvalidInput);
    cfg = tiny();
    cfg.cases = 0;
    EXPECT_EQ(benchmark(cfg, fakeMachine(4)).error().code, ErrorCode::InvalidInput);
    cfg = tiny();
    cfg.cases = 100000;
    cfg.elements = 100000; // 2 x 80 GB of results
    EXPECT_EQ(benchmark(cfg, fakeMachine(4)).error().code, ErrorCode::LimitExceeded);
}

TEST(Benchmark, PresetsMatchSpec) {
    EXPECT_EQ(quickPreset().cases, 500);
    EXPECT_EQ(quickPreset().elements, 1000);
    EXPECT_EQ(reportPreset().seed, 20261101U);
    EXPECT_EQ(reportPreset().workers, (std::vector<int>{1, 2, 4}));
}

TEST(BenchmarkReportIO, WritesAllFourFiles) {
    const auto r = benchmark(tiny(), fakeMachine(2));
    ASSERT_TRUE(r.ok());
    ReportProvenance prov;
    prov.tool = "test";
    prov.status = "PROVISIONAL";
    prov.command = "uniforge_cli bench --report";
    const auto dir = std::filesystem::temp_directory_path() / "uniforge_bench_report_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    ASSERT_TRUE(saveBenchmarkReport(r.value(), prov, dir, "bench_test").ok());
    for (const char* ext : {".json", ".csv", ".md", ".meta.json", "_timing.svg", "_speedup.svg",
                            "_efficiency.svg"}) {
        EXPECT_TRUE(std::filesystem::exists(dir / (std::string("bench_test") + ext))) << ext;
    }
    std::stringstream text;
    {
        std::ifstream csv(dir / "bench_test.csv");
        text << csv.rdbuf();
    }
    EXPECT_EQ(text.str().rfind("workers,threads,warmup_s,rep1_s", 0), 0U);
    const std::string json = benchmarkJson(r.value(), prov);
    EXPECT_NE(json.find("\"max_abs_diff_C\": 0"), std::string::npos);
    EXPECT_NE(json.find("\"status\": \"PROVISIONAL\""), std::string::npos);
    std::filesystem::remove_all(dir, ec);
}
