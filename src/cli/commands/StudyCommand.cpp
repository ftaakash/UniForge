#include "cli/commands/StudyCommand.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "cli/commands/Command.hpp"
#include "cli/platform/Process.hpp"
#include "core/common/AtomicFile.hpp"
#include "core/common/CsvWriter.hpp"
#include "core/common/MetaJson.hpp"
#include "core/study/Collect.hpp"
#include "core/study/Plan.hpp"
#include "core/study/Runner.hpp"

namespace uniforge::cli {

namespace {

using Clock = std::chrono::steady_clock;

struct StudyArgs {
    std::string sub;
    std::vector<std::string> only;
    std::string id;
    bool dry = false;
    bool allowDirty = false;
    std::filesystem::path out = "results/v2";
};

std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == ',') {
            if (!cur.empty()) {
                out.push_back(cur);
            }
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    return out;
}

void usage(std::ostream& os) {
    os << "Usage: uniforge_cli study <plan|run|batch|collect> [options]\n"
          "  plan     list the runs and write DIR/manifest.json\n"
          "  run      --id ID: execute one run in this process (used by batch)\n"
          "  batch    run every planned run that has no result yet, one process each\n"
          "  collect  write DIR/csv/*.csv and DIR/numbers.json from DIR/runs\n"
          "Options: --only E0,E2  --dry-run  --allow-dirty  --out DIR (default results/v2)\n";
}

std::string statusOf(const study::RunRecord& r) {
    const std::string s = r.get("solve_status");
    return s == "not run" ? "setup " + r.get("setup_status") : s;
}

int doPlan(const StudyArgs& a, std::ostream& out, std::ostream& err) {
    const auto runs = study::planExperiments(a.only, a.dry);
    std::vector<std::string> ids;
    for (const auto& r : runs) {
        out << r.id << "\n";
        ids.push_back(r.id);
    }
    MetaJson m;
    m.set("dry_run", a.dry).set("runs", static_cast<int>(runs.size())).set("ids", ids);
    const auto file = a.out / (a.dry ? "manifest_dry.json" : "manifest.json");
    if (auto w = m.save(file); !w) {
        err << "error: " << w.error().message << "\n";
        return kExitError;
    }
    out << runs.size() << " runs; manifest written to " << file.u8string() << "\n";
    return kExitOk;
}

int doRun(const StudyArgs& a, const std::string& commit, std::ostream& out, std::ostream& err) {
    auto spec = study::findRun(a.id, a.dry);
    if (!spec) {
        err << "error: " << spec.error().message << "\n";
        return kExitUsage;
    }
    const int core = spec.value().threads == 1 ? platform::pinToPerformanceCore() : -1;
    const double load = platform::cpuLoadPercent(0.5);
    const study::RunPaths paths{a.out};
    auto rec = study::executeRun(spec.value(), paths, commit);
    if (!rec) {
        err << "error: " << a.id << ": " << rec.error().message << "\n";
        return kExitError;
    }
    study::RunRecord r = std::move(rec).value();
    r.set("peak_ws_bytes", static_cast<long long>(platform::processMemory().peakWorkingSetBytes));
    r.set("core", static_cast<long long>(core));
    r.set("cpu_load_pct", load);
    if (auto w = writeFileAtomic(paths.runFile(a.id), r.toCsv()); !w) {
        err << "error: " << w.error().message << "\n";
        return kExitError;
    }
    out << a.id << ": " << statusOf(r) << "\n";
    return kExitOk;
}

int doCollect(const StudyArgs& a, std::ostream& out, std::ostream& err) {
    auto s = study::collect(study::RunPaths{a.out});
    if (!s) {
        err << "error: " << s.error().message << "\n";
        return kExitError;
    }
    out << s.value().runs << " results collected into " << (a.out / "csv").u8string() << "\n";
    for (const auto& [status, n] : s.value().perStatus) {
        out << "  " << status << ": " << n << "\n";
    }
    return kExitOk;
}

int doBatch(const StudyArgs& a, const std::string& commit, std::ostream& out, std::ostream& err) {
    if (!a.dry && !a.allowDirty && std::system("git diff --quiet HEAD") != 0) {
        err << "error: the working tree has uncommitted changes; official runs need a clean "
               "tree (build spec D-20). Commit first, or pass --allow-dirty for a test.\n";
        return kExitUsage;
    }
    const auto runs = study::planExperiments(a.only, a.dry);
    const study::RunPaths paths{a.out};
    const auto exe = platform::currentExecutable();
    platform::keepAwake(true);
    const auto t0 = Clock::now();
    int done = 0;
    int executed = 0;
    std::string log;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        const study::RunSpec& r = runs[i];
        if (std::filesystem::exists(paths.runFile(r.id))) {
            ++done; // resume: finished earlier
            continue;
        }
        {
            MetaJson progress;
            const double elapsed = std::chrono::duration<double>(Clock::now() - t0).count();
            progress.set("total", static_cast<int>(runs.size()))
                .set("done", done)
                .set("current", r.id)
                .set("elapsed_s", elapsed)
                .set("updated_utc", formatUtcIso8601(std::chrono::system_clock::now()));
            (void)progress.save(a.out / "progress.json");
        }
        out << "[" << (i + 1) << "/" << runs.size() << "] " << r.id << " ... " << std::flush;
        std::vector<std::string> args{"study", "run", "--id", r.id, "--out", a.out.u8string()};
        if (a.dry) {
            args.push_back("--dry-run");
        }
        const auto start = Clock::now();
        // Memory cap: the solver budget plus room for the mesh, K and the process itself.
        const std::uint64_t cap =
            static_cast<std::uint64_t>(r.memoryBudget) + (std::uint64_t(6) << 30);
        const platform::LimitedRun lr =
            platform::runProcessLimited(exe, args, cap, r.wallTimeCap + 600.0);
        const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
        std::string status;
        if (!std::filesystem::exists(paths.runFile(r.id))) {
            // The child did not write a result: record why, so no run is silently missing.
            study::RunRecord rec;
            rec.set("id", r.id);
            rec.set("experiment", r.experiment);
            rec.set("role", std::string("solve"));
            rec.set("design", study::designName(r.design));
            rec.set("system", study::systemKey(r));
            rec.set("spacing_mm", r.spacing * 1e3);
            rec.set("solver", std::string(solvers::label(r.solver)));
            rec.set("git_commit", commit);
            status = lr.timedOut ? "TimeCap" : (lr.started ? "Crashed" : "NotStarted");
            rec.set("solve_status", status);
            rec.set("total_s_median", seconds);
            rec.set("detail", lr.timedOut
                                  ? "killed by the batch after the wall-time cap"
                                  : "exit code " + std::to_string(lr.exitCode) + " (memory cap " +
                                        std::to_string(cap >> 30) + " GiB or a crash)");
            (void)writeFileAtomic(paths.runFile(r.id), rec.toCsv());
        } else {
            std::ifstream in(paths.runFile(r.id), std::ios::binary);
            std::ostringstream s;
            s << in.rdbuf();
            auto rec = study::RunRecord::fromCsv(s.str());
            status = rec ? statusOf(rec.value()) : "unreadable";
        }
        out << status << " (" << formatShortest(std::round(seconds * 10.0) / 10.0) << " s)\n";
        log += formatUtcIso8601(std::chrono::system_clock::now()) + " " + r.id + " " + status +
               " " + formatShortest(seconds) + "s\n";
        {
            // Read the old log in its own scope: on Windows an open handle makes the atomic
            // rename fail, which kept only the first line in the official V2-8 batch.
            std::string all;
            {
                std::ifstream prev(a.out / "batch.log", std::ios::binary);
                std::ostringstream s;
                s << prev.rdbuf();
                all = s.str();
            }
            all += log;
            log.clear();
            (void)writeFileAtomic(a.out / "batch.log", all);
        }
        ++done;
        ++executed;
    }
    platform::keepAwake(false);
    {
        MetaJson progress;
        const double elapsed = std::chrono::duration<double>(Clock::now() - t0).count();
        progress.set("total", static_cast<int>(runs.size()))
            .set("done", done)
            .set("current", std::string())
            .set("elapsed_s", elapsed)
            .set("updated_utc", formatUtcIso8601(std::chrono::system_clock::now()));
        (void)progress.save(a.out / "progress.json");
    }
    out << executed << " runs executed, " << (static_cast<int>(runs.size()) - executed)
        << " already done.\n";
    return doCollect(a, out, err);
}

} // namespace

int runStudy(const std::vector<std::string>& args, const std::string& gitCommit, std::ostream& out,
             std::ostream& err) {
    StudyArgs a;
    if (args.empty()) {
        usage(err);
        return kExitUsage;
    }
    a.sub = args[0];
    for (std::size_t i = 1; i < args.size(); ++i) {
        const bool hasValue = i + 1 < args.size();
        if (args[i] == "--only" && hasValue) {
            a.only = splitList(args[++i]);
        } else if (args[i] == "--id" && hasValue) {
            a.id = args[++i];
        } else if (args[i] == "--out" && hasValue) {
            a.out = std::filesystem::u8path(args[++i]);
        } else if (args[i] == "--dry-run") {
            a.dry = true;
        } else if (args[i] == "--allow-dirty") {
            a.allowDirty = true;
        } else {
            err << "error: unknown or incomplete study option '" << args[i] << "'\n";
            usage(err);
            return kExitUsage;
        }
    }
    if (a.sub == "plan") {
        return doPlan(a, out, err);
    }
    if (a.sub == "run") {
        if (a.id.empty()) {
            err << "error: study run needs --id\n";
            return kExitUsage;
        }
        return doRun(a, gitCommit, out, err);
    }
    if (a.sub == "batch") {
        return doBatch(a, gitCommit, out, err);
    }
    if (a.sub == "collect") {
        return doCollect(a, out, err);
    }
    usage(err);
    return kExitUsage;
}

} // namespace uniforge::cli
