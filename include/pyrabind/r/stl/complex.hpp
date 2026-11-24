#pragma once

#include "pyrabind/r/caster.hpp"

#include <complex>

namespace pyrabind::r_detail {

template <typename T> struct type_caster<std::complex<T>> {
    std::complex<T> val{};

    bool from_r(SEXP x, bool convert) {
        double re = 0.0;
        double im = 0.0;
        if (!r_to_complex(x, convert, re, im)) {
            return false;
        }
        val = std::complex<T>(static_cast<T>(re), static_cast<T>(im));
        return true;
    }

    std::complex<T>& get() noexcept { return val; }

    static SEXP to_r(const std::complex<T>& c, rv_policy, SEXP) {
        return r_from_complex(static_cast<double>(c.real()), static_cast<double>(c.imag()));
    }
};

} // namespace pyrabind::r_detail
