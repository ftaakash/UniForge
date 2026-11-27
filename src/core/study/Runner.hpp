#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "core/common/Result.hpp"
#include "core/sparse/KernelPool.hpp"
#include "core/study/Plan.hpp"

namespace uniforge::study {

// One result row (build spec §6.3 RunResult): ordered column → text. Every run writes the same
// columns (kColumns), so `study collect` can concatenate them.
class RunRecord {
public:
    void set(const std::string& column, const std::string& value);
    void set(const std::string& column, double value); // non-finite → empty cell
    void set(const std::string& column, long long value);
    [[nodiscard]] std::string get(const std::string& column) const;

    // Two lines: header (kColumns) and values. Text is made CSV-safe: ASCII only, no commas,
    // quotes or line breaks (replaced by '?' / ';' / ' ').
    [[nodiscard]] std::string toCsv() const;
    // Parses the output of toCsv() (or any header + one row with the same columns).
    [[nodiscard]] static Result<RunRecord> fromCsv(const std::string& text);

    [[nodiscard]] const std::vector<std::pair<std::string, std::string>>& cells() const noexcept {
        return cells_;
    }

private:
    std::vector<std::pair<std::string, std::string>> cells_;
};

// The column order of every result file.
[[nodiscard]] const std::vector<std::string>& resultColumns();

struct RunPaths {
    std::filesystem::path root; // results/v2
    [[nodiscard]] std::filesystem::path runFile(const std::string& id) const {
        return root / "runs" / (id + ".csv");
    }
    [[nodiscard]] std::filesystem::path historyFile(const std::string& id) const {
        return root / "history" / (id + ".csv");
    }
    [[nodiscard]] std::filesystem::path referenceFile(const std::string& systemKey) const {
        return root / "refs" / (systemKey + ".bin");
    }
};

// Executes one run in the calling process (build spec §7): mesh, assembly, solver setup and
// timed solves with the repetition policy (5 repetitions under 10 s, 3 under 60 s, else 1; the
// first one carries the error observer), engineering outputs and the energy audit. Writes the
// iteration history (first repetition) and, for a Reference run, the reference solution.
// Platform columns (peak memory, core, CPU load, host) are left empty for the caller.
// Errors only for I/O and misuse; a solver failure is a status in the record. `onThreadStart`
// runs on each helper thread of the kernel pool (the caller pins it to a CPU, V3e).
[[nodiscard]] Result<RunRecord>
executeRun(const RunSpec& spec, const RunPaths& paths, const std::string& gitCommit,
           const sparse::KernelPool::ThreadStartFn& onThreadStart = {});

// V3f: writes the run's system in PETSc's binary format for an external reference solver:
// dir/<id>.K.petsc (Mat: classid 1211216, rows, cols, nnz, row lengths, column indices, values)
// and dir/<id>.f.petsc (Vec: classid 1211214, n, values), all big-endian (int32, double).
// If `paths` holds a reference solution for the system, it is written as dir/<id>.ref.petsc
// (Vec) so the external solver can report its error. Returns the number of rows.
[[nodiscard]] Result<std::size_t> exportPetsc(const RunSpec& spec, const RunPaths& paths,
                                              const std::filesystem::path& dir);

// Reference solutions: raw doubles with a small header (n). Saved atomically.
Result<void> saveReference(const std::filesystem::path& file, const std::vector<double>& T);
[[nodiscard]] Result<std::vector<double>> loadReference(const std::filesystem::path& file);

} // namespace uniforge::study
