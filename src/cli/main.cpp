// uniforge_cli: regenerates the evidence in results/ from the same core the GUI uses.
//
//   uniforge_cli fea [--out DIR]    Task C convergence study, CSVs + plots
//   uniforge_cli all [--out DIR]    every evidence generator (Phase 2: fea)
//
// DIR defaults to "results" relative to the current folder.

#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "cli/commands/Command.hpp"
#include "cli/commands/FeaCommand.hpp"

namespace {

using namespace uniforge::cli;

void printUsage(std::ostream& os) {
    os << "Usage: uniforge_cli <command> [--out DIR]\n"
          "\n"
          "Commands:\n"
          "  fea    Thermal FEA convergence study (Task C): N = 10, 20, 40, 80\n"
          "  all    Run every evidence generator\n"
          "\n"
          "Options:\n"
          "  --out DIR    Evidence folder (default: results). CSVs go to DIR/csv,\n"
          "               plots to DIR/plots.\n"
          "  --help       Show this help\n"
          "  --version    Show the version and git commit\n"
          "\n"
          "Exit codes: 0 ok, 1 error, 2 bad command line, 3 a verification check failed.\n";
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
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--out" && i + 1 < args.size()) {
            outDir = std::filesystem::u8path(args[++i]);
        } else {
            std::cerr << "error: unknown or incomplete option '" << args[i] << "'\n\n";
            printUsage(std::cerr);
            return kExitUsage;
        }
    }

    const CommandContext ctx{outDir,
                             UNIFORGE_VERSION,
                             UNIFORGE_GIT_COMMIT,
                             std::chrono::system_clock::now(),
                             std::cout,
                             std::cerr};
    if (command == "fea" || command == "all") {
        return runFea(ctx);
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
