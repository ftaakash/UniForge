#pragma once

#include <cassert>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "core/common/Error.hpp"

namespace uniforge {

// Holds either a value of type T or an Error (spec §5.1). The core API never throws; every
// fallible function returns a Result instead. Construct implicitly from a T or an Error:
//
//     Result<double> safeDiv(double a, double b) {
//         if (b == 0.0) return makeError(ErrorCode::InvalidInput, "division by zero");
//         return a / b;
//     }
//
// Calling value() on an error (or error() on a value) is a programming bug, caught by an
// assertion in debug builds; callers check ok() first.
template <class T> class [[nodiscard]] Result {
    static_assert(!std::is_same_v<std::decay_t<T>, Error>, "Result<Error> is ambiguous");

public:
    Result(const T& value) : data_(std::in_place_index<0>, value) {}
    Result(T&& value) : data_(std::in_place_index<0>, std::move(value)) {}
    Result(Error error) : data_(std::in_place_index<1>, std::move(error)) {}

    [[nodiscard]] bool ok() const noexcept { return data_.index() == 0; }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] T& value() & {
        assert(ok());
        return *std::get_if<0>(&data_);
    }
    [[nodiscard]] const T& value() const& {
        assert(ok());
        return *std::get_if<0>(&data_);
    }
    [[nodiscard]] T&& value() && {
        assert(ok());
        return std::move(*std::get_if<0>(&data_));
    }

    [[nodiscard]] const Error& error() const& {
        assert(!ok());
        return *std::get_if<1>(&data_);
    }
    [[nodiscard]] Error&& error() && {
        assert(!ok());
        return std::move(*std::get_if<1>(&data_));
    }

private:
    std::variant<T, Error> data_;
};

// Success carries no value; failure carries an Error. Default-constructed = success.
template <> class [[nodiscard]] Result<void> {
public:
    Result() = default;
    Result(Error error) : error_(std::move(error)) {}

    [[nodiscard]] bool ok() const noexcept { return !error_.has_value(); }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] const Error& error() const& {
        assert(!ok());
        return *error_;
    }
    [[nodiscard]] Error&& error() && {
        assert(!ok());
        return std::move(*error_);
    }

private:
    std::optional<Error> error_;
};

} // namespace uniforge
