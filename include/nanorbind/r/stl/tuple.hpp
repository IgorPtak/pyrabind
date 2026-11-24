#pragma once

#include "nanorbind/r/caster.hpp"

#include <cpp11/protect.hpp>
#include <tuple>
#include <utility>

namespace nanorbind::r_detail {

template <typename... Ts> struct type_caster<std::tuple<Ts...>> {
    std::tuple<Ts...> val;

    bool from_r(SEXP x, bool convert) {
        if (TYPEOF(x) != VECSXP || static_cast<std::size_t>(Rf_xlength(x)) != sizeof...(Ts)) {
            return false;
        }
        return read_elements(x, convert, std::index_sequence_for<Ts...>{});
    }

    std::tuple<Ts...>& get() noexcept { return val; }

    static SEXP to_r(const std::tuple<Ts...>& tup, rv_policy policy, SEXP parent) {
        SEXP out = cpp11::safe[Rf_allocVector](VECSXP, static_cast<R_xlen_t>(sizeof...(Ts)));
        PROTECT(out);
        write_elements(out, tup, policy, parent, std::index_sequence_for<Ts...>{});
        UNPROTECT(1);
        return out;
    }

private:
    template <std::size_t... Is>
    bool read_elements(SEXP x, bool convert, std::index_sequence<Is...>) {
        return (([&]() -> bool {
                    type_caster<remove_cvref_t<Ts>> c;
                    if (!c.from_r(VECTOR_ELT(x, static_cast<R_xlen_t>(Is)), convert)) {
                        return false;
                    }
                    std::get<Is>(val) = std::move(c.get());
                    return true;
                })() &&
                ...);
    }

    template <std::size_t... Is>
    static void write_elements(SEXP out, const std::tuple<Ts...>& tup, rv_policy policy,
                               SEXP parent, std::index_sequence<Is...>) {
        (SET_VECTOR_ELT(out, static_cast<R_xlen_t>(Is),
                        to_r_sexp(std::get<Is>(tup), policy, parent)),
         ...);
    }
};

} // namespace nanorbind::r_detail
