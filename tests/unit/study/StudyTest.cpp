// V2 experiment harness (build spec §9): plan, run records, references, one run, collect.

#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "core/common/AtomicFile.hpp"
#include "core/study/Collect.hpp"
#include "core/study/Plan.hpp"
#include "core/study/Runner.hpp"

using namespace uniforge::study;

namespace {

std::filesystem::path tempRoot(const std::string& name) {
    const auto p = std::filesystem::temp_directory_path() / ("uniforge_study_" + name);
    std::filesystem::remove_all(p);
    return p;
}

std::string readAll(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

} // namespace

TEST(StudyPlan, IdsAreUniqueFileSafeAndFindable) {
    for (const bool dry : {false, true}) {
        const auto runs = planExperiments({}, dry);
        ASSERT_FALSE(runs.empty());
        std::set<std::string> ids;
        for (const auto& r : runs) {
            EXPECT_TRUE(ids.insert(r.id).second) << "duplicate " << r.id;
            EXPECT_EQ(r.id.find_first_of("/\\:*?\"<>| ,"), std::string::npos) << r.id;
        }
        const auto found = findRun(runs[runs.size() / 2].id, dry);
        ASSERT_TRUE(found.ok());
        EXPECT_EQ(found.value().id, runs[runs.size() / 2].id);
    }
    EXPECT_FALSE(findRun("no-such-run", false).ok());
}

TEST(StudyPlan, OrderAndReferences) {
    const auto runs = planExperiments({}, false);
    // Largest meshes last: the final run belongs to E2 at the finest spacing.
    EXPECT_EQ(runs.back().experiment, "E2");
    EXPECT_NEAR(runs.back().spacing, ladderSpacing(7), 1e-12);
    // Every solve run with an observer has its reference earlier in the list.
    std::set<std::string> refs;
    for (const auto& r : runs) {
        if (r.role == RunRole::Reference) {
            refs.insert(systemKey(r));
        } else if (r.role == RunRole::Solve && r.observer && !r.singular &&
                   r.solver != uniforge::solvers::SolverKind::DirectND &&
                   r.solver != uniforge::solvers::SolverKind::DirectRCM) {
            EXPECT_EQ(refs.count(systemKey(r)), 1U) << r.id;
        }
    }
    // The dry run is a smaller plan with tighter caps.
    const auto dry = planExperiments({}, true);
    EXPECT_LT(dry.size(), runs.size());
    EXPECT_LE(dry.front().wallTimeCap, 300.0);
}

TEST(StudyPlan, LadderAndDesigns) {
    EXPECT_NEAR(ladderSpacing(0), 1e-3, 1e-15);
    EXPECT_NEAR(ladderSpacing(3), 0.5e-3, 1e-15);
    RunSpec r;
    r.design = DesignKey::HS7;
    r.singular = true;
    const auto d = makeDesign(r);
    EXPECT_EQ(d.boxes[0].faces[uniforge::fem3d::ZMax].h, 0.0) << "singular: h = 0";
    r.singular = false;
    r.kTim = 0.5;
    EXPECT_EQ(makeDesign(r).materials[2].k, 0.5);
    EXPECT_NE(systemKey(r), systemKey(RunSpec{}));
}

TEST(StudyRecord, CsvRoundTripAndSanitising) {
    RunRecord r;
    r.set("id", std::string("X"));
    r.set("detail", std::string("a, b \"c\" \xE2\x89\xA4 d\nnext")); // contains U+2264
    r.set("dof", 42LL);
    r.set("kappa_est", 1.5);
    r.set("T_max_C", std::nan(""));
    const std::string csv = r.toCsv();
    EXPECT_EQ(csv.find('"'), std::string::npos);
    auto back = RunRecord::fromCsv(csv);
    ASSERT_TRUE(back.ok());
    EXPECT_EQ(back.value().get("id"), "X");
    EXPECT_EQ(back.value().get("detail"), "a; b  c  ? d next");
    EXPECT_EQ(back.value().get("dof"), "42");
    EXPECT_EQ(back.value().get("kappa_est"), "1.5");
    EXPECT_EQ(back.value().get("T_max_C"), "");
    EXPECT_EQ(back.value().cells().size(), resultColumns().size());
}

TEST(StudyRecord, ReferenceRoundTrip) {
    const auto root = tempRoot("ref");
    const std::vector<double> T{1.0, -2.5, 3.25e10};
    ASSERT_TRUE(saveReference(root / "r.bin", T).ok());
    auto back = loadReference(root / "r.bin");
    ASSERT_TRUE(back.ok());
    EXPECT_EQ(back.value(), T);
    EXPECT_FALSE(loadReference(root / "missing.bin").ok());
    std::filesystem::remove_all(root);
}

TEST(StudyRun, SmokePlanRunsAndCollects) {
    const auto root = tempRoot("run");
    const RunPaths paths{root};
    for (const auto& spec : planExperiments({"T0"}, false)) {
        auto rec = executeRun(spec, paths, "test");
        ASSERT_TRUE(rec.ok()) << spec.id << ": " << rec.error().message;
        EXPECT_EQ(rec.value().get("solve_status"), "Converged") << spec.id;
        ASSERT_TRUE(uniforge::writeFileAtomic(paths.runFile(spec.id), rec.value().toCsv()).ok());
        if (spec.role == RunRole::Solve) {
            EXPECT_NEAR(std::stod(rec.value().get("T_max_C")), 188.2, 1e-6) << spec.id;
            EXPECT_FALSE(rec.value().get("reps").empty());
        }
    }
    EXPECT_TRUE(std::filesystem::exists(root / "refs" / "bar-s10.000.bin"));
    EXPECT_TRUE(std::filesystem::exists(root / "history" / "T0-bar-s10.000-PCG-J.csv"));
    auto summary = collect(paths);
    ASSERT_TRUE(summary.ok());
    EXPECT_EQ(summary.value().runs, 4);
    EXPECT_EQ(summary.value().perStatus.at("Converged"), 4);
    EXPECT_NE(readAll(root / "csv" / "T0.csv").find("T0-bar-s10.000-AMG-PCG"), std::string::npos);
    EXPECT_TRUE(std::filesystem::exists(root / "numbers.json"));
    std::filesystem::remove_all(root);
}

TEST(StudyRun, VerificationChecksReproduceTheTests) {
    const auto root = tempRoot("verify");
    const RunPaths paths{root};
    for (const std::string id : {"E1-verify-bar", "E1-verify-fin"}) {
        auto spec = findRun(id, false);
        ASSERT_TRUE(spec.ok());
        auto rec = executeRun(spec.value(), paths, "test");
        ASSERT_TRUE(rec.ok());
        const double fe = std::stod(rec.value().get("check_fe"));
        const double ref = std::stod(rec.value().get("check_reference"));
        EXPECT_NEAR(fe / ref, 1.0, 2e-3) << id;
    }
    std::filesystem::remove_all(root);
}
