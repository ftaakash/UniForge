#pragma once

#include <chrono>
#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

namespace uniforge::cli {

// `bench` options (Task D); empty strings mean "use the preset's value".
struct BenchArgs {
    std::string seed, cases, elements, workers; // --seed, --cases, --elements, --workers 1,2,4
    bool quick = false;                         // --quick: 500 cases x 1000 elements
    bool report = false;                        // --report: 10000 x 8000 (default)
    std::string status;                         // --status TEXT: label in the report header
};

// Everything a subcommand needs from the command line and the build.
struct CommandContext {
    std::filesystem::path outDir;  // evidence root; CSVs go to <outDir>/csv, plots to <outDir>/plots
    std::filesystem::path dataDir; // sample data root (gen-samples): <dataDir>/geometry, /thermal
    std::vector<std::string> args; // positional arguments after the command (inspect: files)
    std::string sizes;             // import-bench: comma-separated element counts, empty = default
    std::string version;
    std::string gitCommit;
    std::chrono::system_clock::time_point startedAt; // written only into the .meta.json sidecars
    std::ostream& out;
    std::ostream& err;
    BenchArgs bench;               // bench: preset and overrides
};

// Process exit codes shared by all subcommands.
enum ExitCode : int {
    kExitOk = 0,
    kExitError = 1,       // an operation failed (message on stderr)
    kExitUsage = 2,       // bad command line
    kExitCheckFailed = 3, // evidence written, but a verification check did not pass
};

} // namespace uniforge::cli
