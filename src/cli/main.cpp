// uniforge_cli: regenerates the evidence in results/ and the samples in data/ from the same core
// the GUI uses.
//
//   uniforge_cli fea [--out DIR]                     Task C convergence study, CSVs + plots
//   uniforge_cli gen-samples [--data DIR]            Task B sample STL/CSV files
//   uniforge_cli inspect FILE...                     import .stl/.csv files and report
//   uniforge_cli import-bench [--sizes N,...] [--out DIR]   measured import limits
//   uniforge_cli bench [--quick|--report] [--seed S] [--cases C] [--elements N] [--workers 1,2,4]
//                                                    Task D multicore benchmark report
//   uniforge_cli backprop [--out DIR]                Task F gradient check + loss curve
//   uniforge_cli all [--out DIR] [--data DIR]        fea + backprop + gen-samples + quick bench
//
// DIR defaults to "results" (--out) and "data" (--data) relative to the current folder.

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "cli/commands/BackpropCommand.hpp"
#include "cli/commands/BenchCommand.hpp"
#include "cli/commands/Command.hpp"
#include "cli/commands/FeaCommand.hpp"
#include "cli/commands/ImportBenchCommand.hpp"
#include "cli/commands/SamplesCommand.hpp"

namespace {

using namespace uniforge::cli;

void printUsage(std::ostream& os) {
    os << "Usage: uniforge_cli <command> [options]\n"
          "\n"
          "Commands:\n"
          "  fea            Thermal FEA convergence study (Task C): N = 10, 20, 40, 80\n"
          "  gen-samples    Write the sample STL and SYNTHETIC CSV files (Task B)\n"
          "  inspect FILE.. Import .stl / .csv files and print the summary or the error\n"
          "  import-bench   Measure import time and peak memory for 10k/100k/1M elements\n"
          "                 (writes about 0.4 GB of temporary files, deleted afterwards)\n"
          "  bench          Serial vs parallel batch benchmark (Task D): 1 warm-up + 5 timed\n"
          "                 reps per p, report in DIR/benchmarks/bench_<date>.{json,csv,md}\n"
          "  backprop       One-neuron backprop (Task F): gradients vs central differences,\n"
          "                 one update and the 100-step loss curve\n"
          "  all            fea + backprop + gen-samples + a quick bench into DIR/benchmarks/quick\n"
          "                 (import-bench and bench --report are machine-specific, run alone)\n"
          "\n"
          "Options:\n"
          "  --out DIR      Evidence folder (default: results). CSVs go to DIR/csv,\n"
          "                 plots to DIR/plots, import-bench reports to DIR/benchmarks.\n"
          "  --data DIR     Sample data folder for gen-samples (default: data)\n"
          "  --sizes N,...  Element counts for import-bench (default: 10000,100000,1000000)\n"
          "  --quick        bench preset: 500 cases x 1000 elements (about a second)\n"
          "  --report       bench preset: 10000 cases x 8000 elements, seed 20261101 (default)\n"
          "  --seed S, --cases C, --elements N, --workers P,...\n"
          "                 bench overrides (workers are capped at the logical CPU count)\n"
          "  --status TEXT  bench: label written at the top of the report\n"
          "  --help         Show this help\n"
          "  --version      Show the version and git commit\n"
          "\n"
          "Exit codes: 0 ok, 1 error (or a file rejected by inspect), 2 bad command line,\n"
          "3 a verification check failed.\n";
}

int run(const std::vector<std::string>& args) {
    if (args.empty()) {
        printUsage(std::cerr);
        return kExitUsage;
    }
    if (args[0] == "--help" || args[0] == "-h") {
        printUsage(std::cout);
        return kExitOk;
    }
    if (args[0] == "--version") {
        std::cout << "uniforge_cli " << UNIFORGE_VERSION << " (" << UNIFORGE_GIT_COMMIT << ")\n";
        return kExitOk;
    }

    const std::string& command = args[0];
    std::filesystem::path outDir = "results";
    std::filesystem::path dataDir = "data";
    std::string sizes;
    std::vector<std::string> positional;
    BenchArgs bench;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const bool hasValue = i + 1 < args.size();
        if (args[i] == "--quick") {
            bench.quick = true;
        } else if (args[i] == "--report") {
            bench.report = true;
        } else if (args[i] == "--seed" && hasValue) {
            bench.seed = args[++i];
        } else if (args[i] == "--cases" && hasValue) {
            bench.cases = args[++i];
        } else if (args[i] == "--elements" && hasValue) {
            bench.elements = args[++i];
        } else if (args[i] == "--workers" && hasValue) {
            bench.workers = args[++i];
        } else if (args[i] == "--status" && hasValue) {
            bench.status = args[++i];
        } else if (args[i] == "--out" && hasValue) {
            outDir = std::filesystem::u8path(args[++i]);
        } else if (args[i] == "--data" && hasValue) {
            dataDir = std::filesystem::u8path(args[++i]);
        } else if (args[i] == "--sizes" && hasValue) {
            sizes = args[++i];
        } else if (args[i].rfind("--", 0) != 0) {
            positional.push_back(args[i]);
        } else {
            std::cerr << "error: unknown or incomplete option '" << args[i] << "'\n\n";
            printUsage(std::cerr);
            return kExitUsage;
        }
    }
    const bool takesFiles = command == "inspect" || command == "import-bench-worker";
    if (!positional.empty() && !takesFiles) {
        std::cerr << "error: unexpected argument '" << positional.front() << "'\n\n";
        printUsage(std::cerr);
        return kExitUsage;
    }

    const CommandContext ctx{outDir,
                             dataDir,
                             positional,
                             sizes,
                             UNIFORGE_VERSION,
                             UNIFORGE_GIT_COMMIT,
                             std::chrono::system_clock::now(),
                             std::cout,
                             std::cerr,
                             bench};
    if (command == "fea") {
        return runFea(ctx);
    }
    if (command == "backprop") {
        return runBackprop(ctx);
    }
    if (command == "gen-samples") {
        return runGenSamples(ctx);
    }
    if (command == "inspect") {
        return runInspect(ctx);
    }
    if (command == "import-bench") {
        return runImportBench(ctx);
    }
    if (command == "import-bench-worker") { // internal, started by import-bench
        return runImportBenchWorker(ctx);
    }
    if (command == "bench") {
        return runBench(ctx);
    }
    if (command == "all") {
        const int fea = runFea(ctx);
        ctx.out << "\n";
        const int backprop = runBackprop(ctx);
        ctx.out << "\n";
        const int samples = runGenSamples(ctx);
        ctx.out << "\n";
        const int quickBench = runQuickBench(ctx);
        // Report the most serious outcome: error (1) over check failure (3) over ok (0).
        if (fea == kExitError || backprop == kExitError || samples == kExitError ||
            quickBench == kExitError) {
            return kExitError;
        }
        return std::max({fea, backprop, samples, quickBench});
    }
    std::cerr << "error: unknown command '" << command << "'\n\n";
    printUsage(std::cerr);
    return kExitUsage;
}

} // namespace

int main(int argc, char** argv) {
    // The core reports failures as Result values; this only catches std::bad_alloc and other
    // unexpected exceptions so the process still exits with a message and a non-zero code.
    try {
        return run(std::vector<std::string>(argv + 1, argv + argc));
    } catch (const std::exception& e) {
        std::cerr << "internal error: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "internal error: unknown exception\n";
    }
    return kExitError;
}
