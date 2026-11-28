#include "core/study/Collect.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

#include "core/common/AtomicFile.hpp"
#include "core/common/CsvWriter.hpp"
#include "core/common/MetaJson.hpp"

namespace uniforge::study {

namespace {

std::string readAll(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

double num(const RunRecord& r, const std::string& column) {
    const std::string v = r.get(column);
    if (v.empty()) {
        return std::nan("");
    }
    try {
        return std::stod(v);
    } catch (...) {
        return std::nan("");
    }
}

std::string joinRows(const std::vector<RunRecord>& rows) {
    std::string out;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const std::string csv = rows[i].toCsv();
        const auto nl = csv.find('\n');
        if (i == 0) {
            out += csv.substr(0, nl + 1);
        }
        out += csv.substr(nl + 1);
    }
    return out;
}

// (time_s, error_K) pairs from a history file; empty errors are skipped.
std::vector<std::pair<double, double>> readHistory(const std::filesystem::path& p) {
    std::vector<std::pair<double, double>> out;
    std::istringstream in(readAll(p));
    std::string line;
    std::getline(in, line); // header
    while (std::getline(in, line)) {
        std::vector<std::string> c;
        std::string cell;
        for (const char ch : line) {
            if (ch == ',') {
                c.push_back(cell);
                cell.clear();
            } else if (ch != '\r') {
                cell += ch;
            }
        }
        c.push_back(cell);
        if (c.size() == 4 && !c[3].empty()) {
            out.emplace_back(std::stod(c[1]), std::stod(c[3]));
        }
    }
    return out;
}

std::string cell(double v) {
    return std::isfinite(v) ? formatShortest(v) : std::string();
}

} // namespace

Result<CollectSummary> collect(const RunPaths& paths) {
    CollectSummary summary;
    std::vector<RunRecord> rows;
    const auto runsDir = paths.root / "runs";
    if (std::filesystem::exists(runsDir)) {
        std::vector<std::filesystem::path> files;
        for (const auto& e : std::filesystem::directory_iterator(runsDir)) {
            if (e.path().extension() == ".csv") {
                files.push_back(e.path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const auto& f : files) {
            auto r = RunRecord::fromCsv(readAll(f));
            if (!r) {
                return makeError(ErrorCode::ParseError,
                                 "Result file " + f.filename().string() + ": " + r.error().message);
            }
            rows.push_back(std::move(r).value());
        }
    }
    summary.runs = static_cast<int>(rows.size());

    std::map<std::string, std::vector<RunRecord>> byExperiment;
    for (const RunRecord& r : rows) {
        byExperiment[r.get("experiment")].push_back(r);
        ++summary.perExperiment[r.get("experiment")];
        const std::string status = r.get("solve_status") == "not run"
                                       ? "setup " + r.get("setup_status")
                                       : r.get("solve_status");
        ++summary.perStatus[status];
    }
    const auto csvDir = paths.root / "csv";
    if (!rows.empty()) {
        if (auto w = writeFileAtomic(csvDir / "all_runs.csv", joinRows(rows)); !w) {
            return std::move(w).error();
        }
    }
    for (const auto& [experiment, list] : byExperiment) {
        if (auto w = writeFileAtomic(csvDir / (experiment + ".csv"), joinRows(list)); !w) {
            return std::move(w).error();
        }
    }

    // E5: one factorisation / hierarchy, k load cases (amendment A6: derived from E2's timings).
    {
        std::string out =
            "system,spacing_mm,dof,solver,setup_s,solve_s,total_k1_s,total_k5_s,total_k20_s\n";
        for (const RunRecord& r : byExperiment["E2"]) {
            if (r.get("role") != "solve" || r.get("solve_status") != "Converged") {
                continue;
            }
            const double setup = num(r, "setup_s");
            const double solve = num(r, "solve_s_median");
            out += r.get("system") + "," + r.get("spacing_mm") + "," + r.get("dof") + "," +
                   r.get("solver") + "," + cell(setup) + "," + cell(solve) + "," +
                   cell(setup + solve) + "," + cell(setup + 5.0 * solve) + "," +
                   cell(setup + 20.0 * solve) + "\n";
        }
        if (auto w = writeFileAtomic(csvDir / "E5_load_cases.csv", out); !w) {
            return std::move(w).error();
        }
    }

    // E2 time-to-accuracy (amendment A2).
    {
        // T_max per spacing (from any converged solve; all agree), then the Richardson estimate
        // e(s_k) ≈ |T(s_k) − T(s_k+1)| / (r^2 − 1), r = 2^(1/3) the spacing ratio, order 2.
        std::map<double, double> tmax; // spacing_mm → T_max
        for (const RunRecord& r : byExperiment["E2"]) {
            if (r.get("role") == "solve" && r.get("solve_status") == "Converged" &&
                r.get("observer") == "1") {
                tmax.emplace(num(r, "spacing_mm"), num(r, "T_max_C"));
            }
        }
        const double denom = std::pow(2.0, 2.0 / 3.0) - 1.0;
        std::map<double, double> disc;
        for (auto it = tmax.begin(); it != tmax.end(); ++it) {
            // std::map is ascending in spacing: the next finer mesh is the previous entry.
            if (it != tmax.begin()) {
                const auto finer = std::prev(it);
                disc[it->first] = std::fabs(it->second - finer->second) / denom;
            }
        }
        std::string out = "system,spacing_mm,dof,solver,setup_s,disc_error_estimate_K,"
                          "time_to_1mK_s,time_to_1pct_disc_s\n";
        for (const RunRecord& r : byExperiment["E2"]) {
            if (r.get("role") != "solve" || r.get("observer") != "1" ||
                r.get("solve_status") != "Converged") {
                continue;
            }
            const double s = num(r, "spacing_mm");
            const double setup = num(r, "setup_s");
            const double e = disc.count(s) > 0 ? disc[s] : std::nan("");
            const bool direct = r.get("solver").rfind("DIRECT", 0) == 0;
            double t1 = std::nan(""), t2 = std::nan("");
            if (direct) {
                t1 = t2 = num(r, "total_s_median");
            } else {
                const auto hist = readHistory(paths.historyFile(r.get("id")));
                for (const auto& [t, err] : hist) {
                    if (!std::isfinite(t1) && err <= 1e-3) {
                        t1 = setup + t;
                    }
                    if (!std::isfinite(t2) && std::isfinite(e) && err <= 0.01 * e) {
                        t2 = setup + t;
                    }
                }
            }
            out += r.get("system") + "," + r.get("spacing_mm") + "," + r.get("dof") + "," +
                   r.get("solver") + "," + cell(setup) + "," + cell(e) + "," + cell(t1) + "," +
                   cell(t2) + "\n";
        }
        if (auto w = writeFileAtomic(csvDir / "E2_time_to_accuracy.csv", out); !w) {
            return std::move(w).error();
        }
    }

    MetaJson counts;
    for (const auto& [e, n] : summary.perExperiment) {
        counts.set(e, n);
    }
    MetaJson statuses;
    for (const auto& [s, n] : summary.perStatus) {
        statuses.set(s, n);
    }
    MetaJson numbers;
    numbers.set("runs", summary.runs).set("per_experiment", counts).set("per_status", statuses);
    if (auto w = numbers.save(paths.root / "numbers.json"); !w) {
        return std::move(w).error();
    }
    return summary;
}

} // namespace uniforge::study
