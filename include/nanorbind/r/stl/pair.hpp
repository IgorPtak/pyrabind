#pragma once

#include "nanorbind/r/caster.hpp"

#include <cpp11/protect.hpp>
#include <utility>

namespace nanorbind::r_detail {

template <typename T1, typename T2> struct type_caster<std::pair<T1, T2>> {
    std::pair<T1, T2> val;

    bool from_r(SEXP x, bool convert) {
        if (TYPEOF(x) != VECSXP || Rf_xlength(x) != 2) {
            return false;
        }
        type_caster<remove_cvref_t<T1>> c1;
        type_caster<remove_cvref_t<T2>> c2;
        if (!c1.from_r(VECTOR_ELT(x, 0), convert) || !c2.from_r(VECTOR_ELT(x, 1), convert)) {
            return false;
        }
        val.first = c1.get();
        val.second = c2.get();
        return true;
    }

    std::pair<T1, T2>& get() noexcept { return val; }

    static SEXP to_r(const std::pair<T1, T2>& p, rv_policy policy, SEXP parent) {
        SEXP out = cpp11::safe[Rf_allocVector](VECSXP, 2);
        PROTECT(out);
        SET_VECTOR_ELT(out, 0, to_r_sexp(p.first, policy, parent));
        SET_VECTOR_ELT(out, 1, to_r_sexp(p.second, policy, parent));
        UNPROTECT(1);
        return out;
    }
};

} // namespace nanorbind::r_detail
