// V2 experiment harness (build spec §9): plan, run records, references, one run, collect.

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

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

// V3f: `study export` writes K, f and the reference in PETSc's binary format (big-endian
// int32 header and indices, big-endian doubles) with the exact sizes PETSc's loaders expect.
TEST(StudyExport, WritesPetscBinaryMatrixVectorAndReference) {
    auto spec = findRun("V3F-HS7-s1.000-AMG-PCG", false);
    ASSERT_TRUE(spec.ok());
    const auto root = tempRoot("export");
    const RunPaths paths{root};
    // A reference of the right size, so the export also writes <id>.ref.petsc.
    const std::size_t rows = 56015; // HS-7 at 1 mm (E2, V3A–V3F)
    std::filesystem::create_directories(paths.referenceFile(systemKey(spec.value())).parent_path());
    ASSERT_TRUE(
        saveReference(paths.referenceFile(systemKey(spec.value())), std::vector<double>(rows, 25.0))
            .ok());
    const auto dir = root / "petsc";
    auto n = exportPetsc(spec.value(), paths, dir);
    ASSERT_TRUE(n.ok()) << n.error().message;
    ASSERT_EQ(n.value(), rows);
    const std::string id = spec.value().id;
    const std::string K = readAll(dir / (id + ".K.petsc"));
    const std::string f = readAll(dir / (id + ".f.petsc"));
    const std::string ref = readAll(dir / (id + ".ref.petsc"));
    const auto be32 = [](const std::string& s, std::size_t at) {
        std::uint32_t v = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            v = (v << 8) | static_cast<unsigned char>(s[at + i]);
        }
        return v;
    };
    ASSERT_GE(K.size(), 16u);
    EXPECT_EQ(be32(K, 0), 1211216u);
    EXPECT_EQ(be32(K, 4), rows);
    EXPECT_EQ(be32(K, 8), rows);
    const std::size_t nnz = be32(K, 12);
    EXPECT_EQ(K.size(), 16 + 4 * rows + 12 * nnz);
    std::size_t sumLengths = 0;
    for (std::size_t i = 0; i < rows; ++i) {
        sumLengths += be32(K, 16 + 4 * i);
    }
    EXPECT_EQ(sumLengths, nnz);
    EXPECT_EQ(be32(f, 0), 1211214u);
    EXPECT_EQ(be32(f, 4), rows);
    EXPECT_EQ(f.size(), 8 + 8 * rows);
    EXPECT_EQ(ref.size(), 8 + 8 * rows);
    // First reference value: 25.0 as a big-endian double is 40 39 00 00 00 00 00 00.
    EXPECT_EQ(static_cast<unsigned char>(ref[8]), 0x40);
    EXPECT_EQ(static_cast<unsigned char>(ref[9]), 0x39);
    std::filesystem::remove_all(root);
}
