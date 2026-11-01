#include "core/perf/SystemInfo.hpp"

// Set by src/core/CMakeLists.txt; the fallback keeps the file compiling on its own.
#ifndef UNIFORGE_BUILD_FLAGS
#define UNIFORGE_BUILD_FLAGS "unknown"
#endif

namespace uniforge::perf {

std::string compilerDescription() {
#if defined(_MSC_FULL_VER)
    return "MSVC " + std::to_string(_MSC_FULL_VER / 10'000'000) + "." +
           std::to_string((_MSC_FULL_VER / 100'000) % 100) + "." +
           std::to_string(_MSC_FULL_VER % 100'000);
#elif defined(__clang__)
    return "Clang " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__) +
           "." + std::to_string(__clang_patchlevel__);
#elif defined(__GNUC__)
    return "GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." +
           std::to_string(__GNUC_PATCHLEVEL__);
#else
    return "unknown compiler";
#endif
}

std::string buildFlags() {
    return UNIFORGE_BUILD_FLAGS;
}

std::string buildTypeDescription() {
#if defined(NDEBUG)
    return "Release";
#else
    return "Debug (not optimised: timings are not representative)";
#endif
}

} // namespace uniforge::perf
