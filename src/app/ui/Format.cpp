#include "app/ui/Format.hpp"

#include <cmath>
#include <cstdio>
#include <iterator>

namespace uniforge::ui {

namespace {

constexpr const char* kMissing = "—";

// snprintf into a std::string. The C locale's '.' decimal point is used: the app never calls
// setlocale, so the formatting is the same on every machine.
template <class... Args> std::string sprint(const char* fmt, Args... args) {
    char buf[64];
    const int n = std::snprintf(buf, sizeof buf, fmt, args...);
    return n < 0 ? std::string() : std::string(buf, static_cast<std::size_t>(n));
}

// Removes "-" from "-0.0000"-style results so a value that rounds to zero has no sign.
std::string unsignZero(std::string s) {
    if (!s.empty() && s[0] == '-' && s.find_first_of("123456789") == std::string::npos) {
        s.erase(0, 1);
    }
    return s;
}

// Prints `value` with 3 significant digits (0.0123 -> "0.0123", 412 -> "412", 41.25 -> "41.3").
std::string threeSignificant(double value) {
    const double a = std::fabs(value);
    if (a >= 99.95) {
        return sprint("%.0f", value);
    }
    if (a >= 9.995) {
        return sprint("%.1f", value);
    }
    return sprint("%.2f", value);
}

} // namespace

std::string formatTemperature(double celsius) {
    if (!std::isfinite(celsius)) {
        return kMissing;
    }
    return unsignZero(sprint("%.4f", celsius)) + " °C";
}

std::string formatFeError(double celsius) {
    if (!std::isfinite(celsius)) {
        return kMissing;
    }
    const double a = std::fabs(celsius);
    if (a >= 1.0 && a < 1e5) {
        return sprint("%.6f", celsius) + " °C";
    }
    return sprint("%.6e", celsius) + " °C";
}

std::string formatRatio(double value) {
    if (!std::isfinite(value)) {
        return kMissing;
    }
    return unsignZero(sprint("%.4f", value));
}

std::string formatDuration(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) {
        return kMissing;
    }
    struct Unit {
        double scale;
        const char* name;
    };
    constexpr Unit kUnits[] = {{1.0, "s"}, {1e-3, "ms"}, {1e-6, "µs"}, {1e-9, "ns"}};
    for (const Unit& u : kUnits) {
        // 999.5 ms rounds to "1000 ms" with 3 digits: show it as "1.00 s" instead.
        if (seconds >= u.scale * 0.9995 || u.scale == 1e-9) {
            return threeSignificant(seconds / u.scale) + " " + u.name;
        }
    }
    return kMissing;
}

std::string formatSpeedup(double speedup) {
    if (!std::isfinite(speedup)) {
        return kMissing;
    }
    return sprint("%.2f", speedup) + "×";
}

std::string formatEfficiency(double fraction) {
    if (!std::isfinite(fraction)) {
        return kMissing;
    }
    return sprint("%.1f", fraction * 100.0) + " %";
}

std::string formatAgreement(double diffCelsius, double tolerance) {
    if (!std::isfinite(diffCelsius)) {
        return kMissing;
    }
    const bool pass = std::fabs(diffCelsius) <= tolerance;
    return sprint("%.2e", diffCelsius) + " °C (" + (pass ? "≤ " : "> ") +
           formatTolerance(tolerance) + (pass ? " ✓)" : " ✕)");
}

std::string formatCoordinate(double metres) {
    if (!std::isfinite(metres)) {
        return kMissing;
    }
    if (metres == 0.0) {
        return "0.00000 m";
    }
    return unsignZero(sprint("%#.6g", metres)) + " m";
}

std::string formatCount(std::uint64_t count) {
    std::string digits = std::to_string(count);
    std::string out;
    out.reserve(digits.size() + digits.size() / 3);
    const std::size_t lead = digits.size() % 3;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i != 0 && (i - lead) % 3 == 0) {
            out += ',';
        }
        out += digits[i];
    }
    return out;
}

std::string formatBytes(std::uint64_t bytes) {
    if (bytes < 1000) {
        return std::to_string(bytes) + " B";
    }
    constexpr const char* kUnits[] = {"KB", "MB", "GB", "TB"};
    double v = static_cast<double>(bytes) / 1000.0;
    std::size_t u = 0;
    while (v >= 999.5 && u + 1 < std::size(kUnits)) {
        v /= 1000.0;
        ++u;
    }
    return threeSignificant(v) + " " + kUnits[u];
}

std::string formatTolerance(double value) {
    if (!std::isfinite(value)) {
        return kMissing;
    }
    std::string s = sprint("%.1e", value); // "1.0e-09"
    const auto dot = s.find(".0e");
    if (dot != std::string::npos) {
        s.erase(dot, 2); // "1e-09"
    }
    return s;
}

} // namespace uniforge::ui
