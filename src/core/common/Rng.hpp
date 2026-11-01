#pragma once

#include <cstdint>
#include <random>

namespace uniforge {

// Portable pseudo-random numbers for the Task D batch generator (spec §5.2, N8).
//
// std::mt19937_64 itself is fully specified by the C++ standard, so its raw 64-bit output is the
// same on MSVC, GCC and Clang. The distributions are not: std::uniform_real_distribution is
// implementation-defined, so the same seed would give different batches per compiler. Rng
// therefore maps the raw output to [a, b) itself:
//
//     u = (next() >> 11) · 2^-53      53 random bits → exactly representable double in [0, 1)
//     x = a + (b − a) · u              clamped below b if rounding ever lands on b
class Rng {
public:
    explicit Rng(std::uint64_t seed)
        : engine_(seed) {}

    // Raw mt19937_64 output.
    std::uint64_t next() { return engine_(); }

    // Uniform double in [0, 1) with 53 bits of randomness.
    double uniform01();

    // Uniform double in [a, b). Requires a < b, both finite (asserted in debug builds).
    double uniform(double a, double b);

private:
    std::mt19937_64 engine_;
};

} // namespace uniforge
