#include "cli/commands/BackpropCommand.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/common/CsvWriter.hpp"
#include "core/common/MetaJson.hpp"
#include "core/common/SvgPlot.hpp"
#include "core/ml/GradientCheck.hpp"
#include "core/ml/TinyNet.hpp"
#include "core/ml/Trainer.hpp"

namespace uniforge::cli {

namespace {

namespace fs = std::filesystem;
using ml::BackpropProblem;
using ml::GradCheck;
using ml::Params;

// The brief quotes its expected values to 6 decimals.
constexpr double kBriefTol = 1e-6;
constexpr double kBriefAfterUpdate[ml::kParamCount] = {0.620946, 0.120946, 1.071068, 0.153788};

std::string fmt(const char* format, double v) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, format, v);
    return buffer;
}

MetaJson problemJson(const BackpropProblem& p) {
    MetaJson init;
    init.set("w", p.init.w).set("b", p.init.b).set("v", p.init.v).set("c", p.init.c);
    MetaJson j;
    j.set("x", p.x).set("y", p.y).set("initial_params", init).set("lr", p.lr).set("steps",
                                                                                  p.steps);
    return j;
}

// Sidecar next to `csvPath`: <stem>.meta.json.
Result<void> writeSidecar(const CommandContext& ctx, const fs::path& csvPath,
                          const std::string& description, const BackpropProblem& p,
                          const std::vector<std::string>& columns, const MetaJson& units,
                          const MetaJson* extra = nullptr) {
    MetaJson meta;
    meta.set("file", csvPath.filename().generic_u8string())
        .set("description", description)
        .set("tool", "uniforge_cli backprop")
        .set("tool_version", ctx.version)
        .set("git_commit", ctx.gitCommit)
        .set("generated_utc", formatUtcIso8601(ctx.startedAt))
        .set("problem", problemJson(p))
        .set("model", "z = w*x + b, h = tanh(z), yhat = v*h + c, e = yhat - y, J = 0.5*e^2")
        .set("gradients", "dc = e, dv = e*h, db = e*v*(1 - h^2), dw = db*x (hand-derived, "
                          "no autodiff)")
        .set("columns", columns)
        .set("units", units)
        .set("number_format", "%.17g");
    if (extra != nullptr) {
        meta.set("details", *extra);
    }
    fs::path sidecar = csvPath;
    sidecar.replace_extension(".meta.json");
    return meta.save(sidecar);
}

Result<void> writeGradientsCsv(const CommandContext& ctx, const BackpropProblem& p,
                               const ml::Forward& f, const GradCheck& check) {
    const std::vector<std::string> cols{"param", "analytic", "numeric", "abs_diff", "pass"};
    CsvWriter csv(cols);
    for (int i = 0; i < ml::kParamCount; ++i) {
        csv.beginRow();
        csv.add(ml::kParamNames[static_cast<std::size_t>(i)]);
        csv.add(ml::get(check.analytic, i));
        csv.add(ml::get(check.numeric, i));
        csv.add(ml::get(check.absDiff, i));
        csv.add(check.passEach[static_cast<std::size_t>(i)] ? "true" : "false");
    }
    const fs::path path = ctx.outDir / "csv" / "backprop_gradients.csv";
    if (auto r = csv.save(path); !r) {
        return r;
    }
    MetaJson units;
    units.set("param", "parameter name")
        .set("analytic", "1 (dJ/dparam, hand-derived)")
        .set("numeric", "1 (central difference (J(p+eps) - J(p-eps)) / (2 eps))")
        .set("abs_diff", "1 (|analytic - numeric|)")
        .set("pass", "abs_diff <= tolerance");
    MetaJson forward;
    forward.set("z", f.z).set("h", f.h).set("yhat", f.yhat).set("e", f.err).set("J", f.loss);
    MetaJson details;
    details.set("eps", check.eps)
        .set("tolerance", check.tolerance)
        .set("all_pass", check.pass)
        .set("forward", forward);
    return writeSidecar(ctx, path,
                        "Analytic gradients vs central finite differences at the initial "
                        "parameters",
                        p, cols, units, &details);
}

Result<void> writeUpdateCsv(const CommandContext& ctx, const BackpropProblem& p,
                            const ml::Grads& g, const Params& next) {
    const std::vector<std::string> cols{"param", "before", "gradient", "after"};
    CsvWriter csv(cols);
    for (int i = 0; i < ml::kParamCount; ++i) {
        csv.beginRow();
        csv.add(ml::kParamNames[static_cast<std::size_t>(i)]);
        csv.add(ml::get(p.init, i));
        csv.add(ml::get(g, i));
        csv.add(ml::get(next, i));
    }
    const fs::path path = ctx.outDir / "csv" / "backprop_update.csv";
    if (auto r = csv.save(path); !r) {
        return r;
    }
    MetaJson units;
    units.set("param", "parameter name")
        .set("before", "1")
        .set("gradient", "1 (dJ/dparam at the initial parameters)")
        .set("after", "1 (before - lr * gradient)");
    return writeSidecar(ctx, path,
                        "One simultaneous gradient-descent update (all gradients from the "
                        "original parameters)",
                        p, cols, units);
}

Result<void> writeLossCurve(const CommandContext& ctx, const BackpropProblem& p,
                            const std::vector<double>& losses, const Params& last) {
    const std::vector<std::string> cols{"step", "loss"};
    CsvWriter csv(cols);
    for (std::size_t k = 0; k < losses.size(); ++k) {
        csv.beginRow();
        csv.add(static_cast<int>(k));
        csv.add(losses[k]);
    }
    const fs::path path = ctx.outDir / "csv" / "loss_curve.csv";
    if (auto r = csv.save(path); !r) {
        return r;
    }
    MetaJson units;
    units.set("step", "updates applied (0 = initial parameters)").set("loss", "1 (J = 0.5*e^2)");
    const ml::Forward f = ml::forward(last, p.x, p.y);
    MetaJson finalParams;
    finalParams.set("w", last.w).set("b", last.b).set("v", last.v).set("c", last.c);
    MetaJson details;
    details.set("final_params", finalParams)
        .set("final_z", f.z)
        .set("final_h", f.h)
        .set("final_yhat", f.yhat)
        .set("final_loss", f.loss);
    if (auto r = writeSidecar(ctx, path, "Training loss over gradient-descent steps", p, cols,
                              units, &details);
        !r) {
        return r;
    }

    PlotSeries series;
    series.label = "J (lr = " + formatShortest(p.lr) + ")";
    series.color = plotcolor::kSeries[1];
    for (std::size_t k = 0; k < losses.size(); ++k) {
        series.x.push_back(static_cast<double>(k));
        series.y.push_back(losses[k]);
    }
    PlotSpec linear;
    linear.title = "Training loss, one sample (x = 1, y = 2)";
    linear.xLabel = "step";
    linear.yLabel = "J = \xC2\xBD e\xC2\xB2";
    linear.series = {series};
    if (auto r = saveSvgPlot(linear, ctx.outDir / "plots" / "loss_curve.svg"); !r) {
        return r;
    }
    // A log axis needs positive values; an exactly-zero loss would end the curve early.
    PlotSpec logy = linear;
    logy.title = "Training loss, log scale";
    logy.logY = true;
    PlotSeries& s = logy.series.front();
    std::size_t positive = 0;
    while (positive < s.y.size() && s.y[positive] > 0.0) {
        ++positive;
    }
    s.x.resize(positive);
    s.y.resize(positive);
    return saveSvgPlot(logy, ctx.outDir / "plots" / "loss_curve_logy.svg");
}

void printTables(const CommandContext& ctx, const ml::Forward& f, const GradCheck& check,
                 const BackpropProblem& p, const Params& next) {
    ctx.out << "Forward pass:  z = " << fmt("%.6f", f.z) << "  h = " << fmt("%.6f", f.h)
            << "  yhat = " << fmt("%.6f", f.yhat) << "  e = " << fmt("%.6f", f.err)
            << "  J = " << fmt("%.6f", f.loss) << "\n\n";
    ctx.out << "  param      analytic       numeric      |diff|   pass (<= 1e-5)\n";
    for (int i = 0; i < ml::kParamCount; ++i) {
        char line[160];
        std::snprintf(line, sizeof line, "  %-5s  %12.6f  %12.6f  %10.3e   %s\n",
                      ml::kParamNames[static_cast<std::size_t>(i)], ml::get(check.analytic, i),
                      ml::get(check.numeric, i), ml::get(check.absDiff, i),
                      check.passEach[static_cast<std::size_t>(i)] ? "yes" : "NO");
        ctx.out << line;
    }
    ctx.out << "\nAfter one update (lr = " << formatShortest(p.lr) << "):  w = "
            << fmt("%.6f", next.w) << "  b = " << fmt("%.6f", next.b)
            << "  v = " << fmt("%.6f", next.v) << "  c = " << fmt("%.6f", next.c) << "\n";
}

// Selected steps of the loss curve with the per-step ratio and h = tanh(z) at that step, so the
// rate and the saturation of tanh can be read from the console (docs/backprop.md uses them).
void printLossSummary(const CommandContext& ctx, const BackpropProblem& p,
                      const std::vector<double>& losses, const Params& last) {
    ctx.out << "\nLoss curve (lr = " << formatShortest(p.lr) << "):\n"
            << "   step              J   J_k / J_k-1         h   1 - h^2\n";
    for (int k : {0, 1, 2, 3, 5, 10, 20, 50, 100}) {
        if (k > p.steps) {
            break;
        }
        Params pk;
        (void)ml::train(p.init, p.x, p.y, p.lr, k, &pk);
        const ml::Forward f = ml::forward(pk, p.x, p.y);
        const auto idx = static_cast<std::size_t>(k);
        const std::string ratio =
            k == 0 ? std::string("-") : fmt("%.6f", losses[idx] / losses[idx - 1]);
        char line[160];
        std::snprintf(line, sizeof line, "  %5d  %13.6e  %12s  %8.6f  %8.6f\n", k, losses[idx],
                      ratio.c_str(), f.h, 1.0 - f.h * f.h);
        ctx.out << line;
    }
    ctx.out << "  after " << p.steps << " steps: w = " << fmt("%.6f", last.w)
            << "  b = " << fmt("%.6f", last.b) << "  v = " << fmt("%.6f", last.v)
            << "  c = " << fmt("%.6f", last.c)
            << "  yhat = " << fmt("%.12f", ml::forward(last, p.x, p.y).yhat) << "\n";
}

// Prints one PASS/FAIL line per check; returns true when all pass.
bool runChecks(const CommandContext& ctx, const GradCheck& check, const Params& next,
               const std::vector<double>& losses) {
    bool all = true;
    const auto line = [&](bool pass, const std::string& what) {
        ctx.out << (pass ? "  PASS  " : "  FAIL  ") << what << "\n";
        all = all && pass;
    };
    int passed = 0;
    double maxDiff = 0.0;
    for (int i = 0; i < ml::kParamCount; ++i) {
        passed += check.passEach[static_cast<std::size_t>(i)] ? 1 : 0;
        maxDiff = std::max(maxDiff, ml::get(check.absDiff, i));
    }
    line(check.pass, std::to_string(passed) + "/4 central-difference checks (eps = 1e-6), max "
                         "|diff| " + fmt("%.3e", maxDiff) + " <= 1e-5");
    double updateDev = 0.0;
    for (int i = 0; i < ml::kParamCount; ++i) {
        updateDev = std::max(updateDev, std::abs(ml::get(next, i) - kBriefAfterUpdate[i]));
    }
    line(updateDev <= kBriefTol, "update matches the brief (max deviation " +
                                     fmt("%.1e", updateDev) + " <= 1e-6)");
    std::size_t increases = 0;
    for (std::size_t k = 1; k < losses.size(); ++k) {
        increases += losses[k] > losses[k - 1] ? 1U : 0U;
    }
    line(increases == 0, "loss never increases over " + std::to_string(losses.size() - 1) +
                             " steps (J: " + fmt("%.6f", losses.front()) + " -> " +
                             fmt("%.3e", losses.back()) + ")");
    return all;
}

} // namespace

int runBackprop(const CommandContext& ctx) {
    const BackpropProblem problem; // the brief's values
    if (auto r = problem.validate(); !r) {
        ctx.err << "error: " << r.error().message << "\n  hint: " << r.error().hint << "\n";
        return kExitError;
    }
    ctx.out << "UniForge backprop (Task F): one neuron, z = wx + b, h = tanh z, yhat = vh + c\n"
            << "  x = " << problem.x << ", y = " << problem.y << ", w = " << problem.init.w
            << ", b = " << problem.init.b << ", v = " << problem.init.v
            << ", c = " << problem.init.c << ", lr = " << problem.lr
            << ", steps = " << problem.steps << "\n\n";

    const ml::Forward f = ml::forward(problem.init, problem.x, problem.y);
    const GradCheck check = ml::centralDiff(problem.init, problem.x, problem.y);
    const Params next = ml::sgdStep(problem.init, check.analytic, problem.lr);
    Params last;
    const std::vector<double> losses =
        ml::train(problem.init, problem.x, problem.y, problem.lr, problem.steps, &last);

    const auto fail = [&](const Error& e) {
        ctx.err << "error: " << e.message << "\n  hint: " << e.hint << "\n";
        return kExitError;
    };
    if (auto r = writeGradientsCsv(ctx, problem, f, check); !r) {
        return fail(r.error());
    }
    if (auto r = writeUpdateCsv(ctx, problem, check.analytic, next); !r) {
        return fail(r.error());
    }
    if (auto r = writeLossCurve(ctx, problem, losses, last); !r) {
        return fail(r.error());
    }

    printTables(ctx, f, check, problem, next);
    printLossSummary(ctx, problem, losses, last);
    ctx.out << "\nChecks:\n";
    const bool pass = runChecks(ctx, check, next, losses);
    ctx.out << "\nWrote " << (ctx.outDir / "csv").generic_u8string()
            << "/{backprop_gradients,backprop_update,loss_curve}.csv (+ .meta.json) and "
            << (ctx.outDir / "plots").generic_u8string() << "/loss_curve{,_logy}.svg\n";
    return pass ? kExitOk : kExitCheckFailed;
}

} // namespace uniforge::cli
