#pragma once

#include "pyrabind/r/caster.hpp"

#include <chrono>

namespace pyrabind::r_detail {

template <typename ChronoT> struct chrono_duration_caster {
    using rep = typename ChronoT::rep;
    using period = typename ChronoT::period;
    using duration_t = std::chrono::duration<rep, period>;

    ChronoT val{};

    bool from_r(SEXP x, bool convert) {
        double secs = 0.0;
        if (!r_to_duration_secs(x, convert, secs)) {
            return false;
        }
        val = ChronoT(std::chrono::duration_cast<duration_t>(std::chrono::duration<double>(secs)));
        return true;
    }

    ChronoT& get() noexcept { return val; }

    static const duration_t& extract_duration(const duration_t& d) noexcept { return d; }

    template <typename Clock>
    static duration_t extract_duration(const std::chrono::time_point<Clock, duration_t>& tp) {
        return tp.time_since_epoch();
    }

    static SEXP to_r(const ChronoT& src, rv_policy, SEXP) {
        const auto d = extract_duration(src);
        const double secs = std::chrono::duration<double>(d).count();
        return r_from_duration_secs(secs);
    }
};

template <typename Duration>
struct type_caster<std::chrono::time_point<std::chrono::system_clock, Duration>> {
    using type = std::chrono::time_point<std::chrono::system_clock, Duration>;
    type val{};

    bool from_r(SEXP x, bool) {
        double epoch_secs = 0.0;
        if (!r_to_posix_secs(x, epoch_secs)) {
            return false;
        }
        val = type(std::chrono::duration_cast<Duration>(std::chrono::duration<double>(epoch_secs)));
        return true;
    }

    type& get() noexcept { return val; }

    static SEXP to_r(const type& tp, rv_policy, SEXP) {
        const double secs = std::chrono::duration<double>(tp.time_since_epoch()).count();
        return r_from_posix_secs(secs);
    }
};

template <typename Clock, typename Duration>
struct type_caster<std::chrono::time_point<Clock, Duration>>
    : chrono_duration_caster<std::chrono::time_point<Clock, Duration>> {};

template <typename Rep, typename Period>
struct type_caster<std::chrono::duration<Rep, Period>>
    : chrono_duration_caster<std::chrono::duration<Rep, Period>> {};

} // namespace pyrabind::r_detail
