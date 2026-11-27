#include "core/study/Plan.hpp"

#include <cmath>
#include <cstdio>
#include <set>
#include <string>

namespace uniforge::study {

namespace {

using solvers::SolverKind;

const std::vector<SolverKind> kAll{SolverKind::DirectND,  SolverKind::DirectRCM,
                                   SolverKind::PcgJacobi, SolverKind::PcgIc0,
                                   SolverKind::Amg,       SolverKind::AmgPcg};

bool isIterative(SolverKind k) {
    return k != SolverKind::DirectND && k != SolverKind::DirectRCM;
}

std::string fixed(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

std::string shortNumber(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

// Parameter tag of a system: empty for the defaults, e.g. "h5", "kTim0.5", "tim0.2mm-c2".
std::string paramTag(const RunSpec& s) {
    std::string tag;
    const auto add = [&](const std::string& t) { tag += tag.empty() ? t : "-" + t; };
    if (s.h != 25.0) {
        add("h" + shortNumber(s.h));
    }
    if (s.kTim != 3.0) {
        add("kTim" + shortNumber(s.kTim));
    }
    if (s.timThickness != 1e-4) {
        add("tim" + shortNumber(s.timThickness * 1e3) + "mm");
    }
    if (s.timCells != 1) {
        add("c" + std::to_string(s.timCells));
    }
    if (s.singular) {
        add("singular");
    }
    return tag;
}

std::string systemPart(const RunSpec& s) {
    const std::string tag = paramTag(s);
    return designName(s.design) + (tag.empty() ? "" : "-" + tag) + "-s" + fixed(s.spacing * 1e3, 3);
}

std::string solverPart(const RunSpec& s) {
    std::string part(solvers::label(s.solver));
    if (s.naturalOrdering) {
        part = "DIRECT-NAT";
    }
    if (s.ic0NoShift) {
        part += "-noshift";
    }
    if (!s.observer) {
        part += "-noobs";
    }
    if (s.threads != 1) {
        part += "-p" + std::to_string(s.threads);
    }
    return part;
}

class Planner {
public:
    explicit Planner(bool dry)
        : dry_(dry) {}

    // A system: its reference (if an iterative solver will track the error), then the solvers.
    void system(const std::string& experiment, RunSpec base, const std::vector<SolverKind>& kinds) {
        base.experiment = experiment;
        if (dry_) {
            base.wallTimeCap = 300.0;
        }
        bool needsReference = false;
        for (const SolverKind k : kinds) {
            needsReference = needsReference || (isIterative(k) && base.observer);
        }
        if (needsReference && !base.singular) {
            RunSpec ref = base;
            ref.role = RunRole::Reference;
            ref.solver = SolverKind::DirectND;
            ref.id = experiment + "-" + systemPart(ref) + "-REF";
            push(ref);
        }
        for (const SolverKind k : kinds) {
            RunSpec r = base;
            r.role = RunRole::Solve;
            r.solver = k;
            r.id = experiment + "-" + systemPart(r) + "-" + solverPart(r);
            push(r);
        }
    }

    void verification(const std::string& check) {
        RunSpec r;
        r.experiment = "E1";
        r.role = RunRole::Verification;
        r.check = check;
        r.relTol = 1e-13;
        r.maxIterations = 200000;
        if (dry_) {
            r.wallTimeCap = 300.0;
        }
        r.id = "E1-verify-" + check;
        push(r);
    }

    std::vector<RunSpec> runs;

private:
    void push(const RunSpec& r) {
        if (ids_.insert(r.id).second) { // the same system may appear in two sweeps once
            runs.push_back(r);
        }
    }
    bool dry_;
    std::set<std::string> ids_;
};

void planE0(Planner& p, bool dry) {
    RunSpec bar;
    bar.design = DesignKey::V1Bar;
    const std::vector<double> barS =
        dry ? std::vector<double>{0.010, 0.005} : std::vector<double>{0.010, 0.005, 0.0025};
    for (const double s : barS) {
        bar.spacing = s;
        p.system("E0", bar, kAll);
    }
    RunSpec cube;
    cube.design = DesignKey::Cube;
    const double a = std::cbrt(1e-4);
    const std::vector<int> cells = dry ? std::vector<int>{8, 16} : std::vector<int>{8, 16, 24};
    for (const int c : cells) {
        cube.spacing = a / c;
        p.system("E0", cube, kAll);
    }
}

void planE1(Planner& p, bool dry) {
    const std::vector<std::string> checks =
        dry ? std::vector<std::string>{"bar", "mms4", "mms8", "fin"}
            : std::vector<std::string>{"bar", "mms4", "mms8", "mms16", "mms32", "fin"};
    for (const auto& c : checks) {
        p.verification(c);
    }
}

void planE2(Planner& p, bool dry) {
    const int kMax = dry ? 1 : 7;
    for (int k = 0; k <= kMax; ++k) {
        RunSpec r;
        r.design = DesignKey::HS7;
        r.spacing = ladderSpacing(k);
        p.system("E2", r, kAll);
        if (k == 2 || (dry && k == 1)) {
            // Observer-overhead check (N4): the same PCG-J solve without the error observer.
            RunSpec noObs = r;
            noObs.observer = false;
            p.system("E2", noObs, {SolverKind::PcgJacobi});
        }
    }
}

void planE3(Planner& p, bool dry) {
    RunSpec base;
    base.design = DesignKey::HS7;
    base.spacing = dry ? ladderSpacing(0) : ladderSpacing(3); // 1 mm or 0.5 mm (~2.9e5 DOF)
    const std::vector<SolverKind> kinds{SolverKind::PcgJacobi, SolverKind::PcgIc0, SolverKind::Amg,
                                        SolverKind::AmgPcg, SolverKind::DirectND};
    const std::vector<double> hs =
        dry ? std::vector<double>{5.0, 500.0} : std::vector<double>{5.0, 25.0, 100.0, 500.0};
    for (const double h : hs) {
        RunSpec r = base;
        r.h = h;
        p.system("E3", r, kinds);
    }
    if (dry) {
        return;
    }
    for (const double k : {0.5, 3.0, 20.0, 200.0}) {
        RunSpec r = base;
        r.kTim = k;
        p.system("E3", r, kinds);
    }
    for (const double t : {1e-4, 2e-4, 5e-4}) {
        for (const int cells : {1, 2}) {
            RunSpec r = base;
            r.timThickness = t;
            r.timCells = cells;
            p.system("E3", r, kinds);
        }
    }
}

void planE4(Planner& p, bool dry) {
    const double s = dry ? 2e-3 : 1e-3;
    const std::vector<DesignKey> ladder =
        dry ? std::vector<DesignKey>{DesignKey::Plate, DesignKey::HS7}
            : std::vector<DesignKey>{DesignKey::V1Bar,       DesignKey::Cube, DesignKey::Plate,
                                     DesignKey::FinnedPlate, DesignKey::HS7,  DesignKey::PinFin};
    for (const DesignKey d : ladder) {
        RunSpec r;
        r.design = d;
        r.spacing = s;
        p.system("E4", r, kAll);
    }
}

void planE6(Planner& p, bool dry) {
    RunSpec base;
    base.design = DesignKey::HS7;
    base.spacing = dry ? ladderSpacing(0) : ladderSpacing(4); // 1 mm or 0.4 mm (~5.7e5 DOF)
    const std::vector<int> threads =
        dry ? std::vector<int>{1, 4} : std::vector<int>{1, 2, 4, 8, 14, 20};
    for (const int t : threads) {
        RunSpec r = base;
        r.threads = t;
        r.observer = false; // timing only; agreement is checked by E2
        p.system("E6", r, {SolverKind::PcgJacobi, SolverKind::AmgPcg});
    }
}

void planE7(Planner& p, bool dry) {
    const int kMax = dry ? 0 : 1;
    for (int k = 0; k <= kMax; ++k) {
        RunSpec r;
        r.design = DesignKey::HS7;
        r.spacing = ladderSpacing(k);
        r.naturalOrdering = true;
        p.system("E7", r, {SolverKind::DirectND});
    }
}

void planE8(Planner& p, bool dry) {
    RunSpec singular;
    singular.design = DesignKey::HS7;
    singular.spacing = 2e-3;
    singular.singular = true;
    singular.observer = false;
    singular.maxIterations = 2000;
    p.system("E8", singular, kAll);
    const std::vector<int> ks = dry ? std::vector<int>{0} : std::vector<int>{0, 3};
    for (const int k : ks) {
        RunSpec r;
        r.design = DesignKey::HS7;
        r.spacing = ladderSpacing(k);
        r.ic0NoShift = true;
        r.observer = false;
        p.system("E8", r, {SolverKind::PcgIc0});
    }
}

// V3a: AMG-PCG with a double against a single-precision hierarchy on the E2 HS-7 ladder.
// Same caps, tolerance and observer as E2, so the rows compare one to one.
void planV3A(Planner& p, bool dry) {
    const int kMax = dry ? 1 : 7;
    for (int k = 0; k <= kMax; ++k) {
        RunSpec r;
        r.design = DesignKey::HS7;
        r.spacing = ladderSpacing(k);
        p.system("V3A", r, {SolverKind::AmgPcg, SolverKind::AmgPcgSp});
    }
    // The same pair with 8 and 20 threads on the largest meshes (timing only, as E6): the
    // parallel kernels are where halving the bytes could show, the sweeps stay serial.
    const std::vector<int> ks = dry ? std::vector<int>{1} : std::vector<int>{5, 6, 7};
    for (const int k : ks) {
        for (const int t : {8, 20}) {
            RunSpec r;
            r.design = DesignKey::HS7;
            r.spacing = ladderSpacing(k);
            r.threads = t;
            r.observer = false;
            p.system("V3A", r, {SolverKind::AmgPcg, SolverKind::AmgPcgSp});
        }
    }
}

// V3b: Chebyshev–Jacobi smoother (parallel) against the serial Gauss–Seidel of AMG-PCG. Ladder
// on one pinned core with the error observer (as E2), then the four largest meshes with 8 and
// 20 threads (timing only, as E6), double and single precision.
void planV3B(Planner& p, bool dry) {
    const int kMax = dry ? 1 : 7;
    for (int k = 0; k <= kMax; ++k) {
        RunSpec r;
        r.design = DesignKey::HS7;
        r.spacing = ladderSpacing(k);
        p.system("V3B", r, {SolverKind::AmgPcg, SolverKind::AmgPcgCheb});
    }
    const std::vector<int> ks = dry ? std::vector<int>{1} : std::vector<int>{4, 5, 6, 7};
    for (const int k : ks) {
        for (const int t : {8, 20}) {
            RunSpec r;
            r.design = DesignKey::HS7;
            r.spacing = ladderSpacing(k);
            r.threads = t;
            r.observer = false;
            p.system("V3B", r, {SolverKind::AmgPcgCheb, SolverKind::AmgPcgChebSp});
        }
    }
}

void planT0(Planner& p) {
    RunSpec r;
    r.design = DesignKey::V1Bar;
    r.spacing = 0.010;
    r.wallTimeCap = 60.0;
    p.system("T0", r, {SolverKind::DirectND, SolverKind::PcgJacobi, SolverKind::AmgPcg});
}

} // namespace

std::string designName(DesignKey key) {
    switch (key) {
    case DesignKey::V1Bar:
        return "bar";
    case DesignKey::Cube:
        return "cube";
    case DesignKey::Plate:
        return "plate";
    case DesignKey::FinnedPlate:
        return "fins";
    case DesignKey::HS7:
        return "HS7";
    case DesignKey::PinFin:
        return "pinfin";
    }
    return "?";
}

fem3d::Design makeDesign(const RunSpec& spec) {
    fem3d::HeatSinkParams p;
    p.h = spec.singular ? 0.0 : spec.h; // h = 0 everywhere: every face insulated, K singular
    p.kTim = spec.kTim;
    p.timThickness = spec.timThickness;
    switch (spec.design) {
    case DesignKey::V1Bar:
        return fem3d::makeV1Bar();
    case DesignKey::Cube:
        return fem3d::makeCube();
    case DesignKey::Plate:
        return fem3d::makePlate(p);
    case DesignKey::FinnedPlate:
        return fem3d::makeFinnedPlate(p);
    case DesignKey::HS7:
        return fem3d::makeHS7(p);
    case DesignKey::PinFin:
        return fem3d::makePinFin(p);
    }
    return fem3d::makeHS7(p);
}

std::string systemKey(const RunSpec& spec) {
    return systemPart(spec);
}

double ladderSpacing(int k) {
    return 1e-3 * std::pow(2.0, -k / 3.0);
}

std::vector<RunSpec> planExperiments(const std::vector<std::string>& experiments, bool dryRun) {
    const std::vector<std::string> order{"T0", "E1", "E0", "E7",  "E8", "E3",
                                         "E4", "E6", "E2", "V3A", "V3B"};
    std::set<std::string> wanted(experiments.begin(), experiments.end());
    Planner p(dryRun);
    for (const auto& e : order) {
        // The default plan is the V2 study (E0–E8); V3 experiments run only when named.
        const bool selected =
            wanted.empty() ? (e != "T0" && e.rfind("V3", 0) != 0) : wanted.count(e) > 0;
        if (!selected) {
            continue;
        }
        if (e == "T0") {
            planT0(p);
        } else if (e == "E0") {
            planE0(p, dryRun);
        } else if (e == "E1") {
            planE1(p, dryRun);
        } else if (e == "E2") {
            planE2(p, dryRun);
        } else if (e == "E3") {
            planE3(p, dryRun);
        } else if (e == "E4") {
            planE4(p, dryRun);
        } else if (e == "E6") {
            planE6(p, dryRun);
        } else if (e == "E7") {
            planE7(p, dryRun);
        } else if (e == "E8") {
            planE8(p, dryRun);
        } else if (e == "V3A") {
            planV3A(p, dryRun);
        } else if (e == "V3B") {
            planV3B(p, dryRun);
        }
    }
    return p.runs;
}

Result<RunSpec> findRun(const std::string& id, bool dryRun) {
    for (const RunSpec& r : planExperiments(
             {"T0", "E0", "E1", "E2", "E3", "E4", "E6", "E7", "E8", "V3A", "V3B"}, dryRun)) {
        if (r.id == id) {
            return r;
        }
    }
    return makeError(ErrorCode::InvalidInput, "No run with id '" + id + "' in the plan.",
                     "List the runs with `uniforge_cli study plan`.");
}

} // namespace uniforge::study
