#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "app/services/JobRunner.hpp"
#include "app/ui/Theme.hpp"

// Form building blocks (spec §8.4): VerificationBadge, InlineAlert, ParamField, FileField,
// RunBar. Each widget explains itself: badges carry numbers and tolerance in the tooltip,
// invalid fields say why, disabled buttons say why.
namespace uniforge::ui {

// ---- VerificationBadge ----------------------------------------------------------------------
enum class Verdict { Pass, Fail, Pending, NotApplicable };

struct Badge {
    std::string label; // "Order ≈ 2"
    Verdict verdict = Verdict::Pending;
    std::string measured;  // "observed order 2.0000 (N = 40 → 80)"
    std::string tolerance; // "pass if |order − 2| ≤ 0.01"
};

// Glyph + label in a rounded chip; tooltip shows measured value and tolerance.
void verificationBadge(const Badge& badge);
// Badges on one line, wrapping when the row is full.
void badgeRow(const std::vector<Badge>& badges);
[[nodiscard]] Verdict verdictOf(bool pass);

// ---- InlineAlert ----------------------------------------------------------------------------
// Bordered message in the status colour: title line (glyph + bold) and wrapped body.
// Errors show Error::message as title and Error::hint as body (spec §5.1: never invented text).
void inlineAlert(Status status, const std::string& title, const std::string& body = {});

// ---- ParamField -----------------------------------------------------------------------------
struct ParamSpec {
    const char* id;    // ImGui id, e.g. "k"
    const char* label; // "Thermal conductivity k"
    const char* unit;  // "W/(m·K)"
    double min;        // valid range (inclusive unless minExclusive)
    double max;
    double defaultValue;
    bool minExclusive = false;
    const char* format = "%.6g";
};

// Label [unit], number input and ↺ reset. Returns true when `value` is valid; otherwise the
// field is outlined in status.error and `reason` explains the allowed range.
bool paramField(const ParamSpec& spec, double& value, std::string& reason);

// ---- FileField ------------------------------------------------------------------------------
enum class FileFieldAction { None, Browse, Clear };

// Label, read-only path box (file name; full path in the tooltip) and Browse… button.
// The caller opens the dialog on Browse and also feeds dropped files.
FileFieldAction fileField(const char* id, const char* label, const std::filesystem::path& path,
                          bool enabled, const char* disabledReason = nullptr);

// ---- RunBar ---------------------------------------------------------------------------------
enum class RunBarAction { None, Run, Cancel };

// Run ↔ Cancel, progress bar, stage text and elapsed time for jobs named `jobName`
// (Ctrl+Enter runs, Esc cancels). When another job holds the runner, Run is disabled with
// a reason. `canRun` false disables Run with `disabledReason`.
RunBarAction runBar(const JobSnapshot& job, const char* jobName, const char* runLabel, bool canRun,
                    const char* disabledReason);

// One-line outcome of the last job named `jobName`: "✓ Done in 12.4 ms", "! Cancelled —
// previous results kept", "✕ Failed: …". Nothing when another job ran last.
void jobOutcome(const JobSnapshot& job, const char* jobName);

} // namespace uniforge::ui
