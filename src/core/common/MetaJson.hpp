#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/common/Result.hpp"

namespace uniforge {

// "2026-11-01T10:22:04Z" (UTC, whole seconds). Pure std::chrono arithmetic, no gmtime, so it is
// thread-safe and identical on every platform.
[[nodiscard]] std::string formatUtcIso8601(std::chrono::system_clock::time_point t);

// Minimal ordered JSON object builder for the `.meta.json` sidecars next to evidence CSVs
// (spec §9: tool version, git hash, parameters, units, UTC time). Keys keep insertion order and
// output is pretty-printed with two-space indents, so a sidecar diff is easy to read.
// Non-finite numbers are written as null (JSON has no NaN).
//
//     MetaJson params;
//     params.set("L_m", 1.0).set("elements", 10);
//     MetaJson meta;
//     meta.set("tool", "uniforge_cli").set("params", params);
class MetaJson {
public:
    MetaJson& set(std::string_view key, std::string_view value);
    MetaJson& set(std::string_view key, const char* value) {
        return set(key, std::string_view(value));
    }
    MetaJson& set(std::string_view key, double value);
    MetaJson& set(std::string_view key, int value);
    MetaJson& set(std::string_view key, bool value);
    MetaJson& set(std::string_view key, const MetaJson& object);
    MetaJson& set(std::string_view key, const std::vector<std::string>& values);
    MetaJson& set(std::string_view key, const std::vector<int>& values);

    [[nodiscard]] std::string str() const { return render(0) + "\n"; }
    Result<void> save(const std::filesystem::path& path) const;

private:
    [[nodiscard]] std::string render(int indent) const;

    // Each value is stored pre-rendered; nested objects keep their builder for indentation.
    struct Entry {
        std::string key;
        std::string scalar;            // rendered JSON scalar or array, when not an object
        std::vector<MetaJson> objects; // exactly one element when the value is an object
    };
    std::vector<Entry> entries_;
};

} // namespace uniforge
