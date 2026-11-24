#pragma once

#include "nanorbind/r/caster.hpp"

#include <cpp11/protect.hpp>
#include <map>
#include <string>
#include <unordered_map>

namespace nanorbind::r_detail {

template <typename MapT> struct string_map_caster {
    MapT val;

    bool from_r(SEXP x, bool convert) {
        val.clear();
        if (TYPEOF(x) != VECSXP) {
            return false;
        }
        const R_xlen_t n = Rf_xlength(x);
        if (n == 0) {
            return true;
        }
        SEXP nms = Rf_getAttrib(x, R_NamesSymbol);
        if (nms == R_NilValue || TYPEOF(nms) != STRSXP || Rf_xlength(nms) != n) {
            return false;
        }
        using ValT = typename MapT::mapped_type;
        for (R_xlen_t i = 0; i < n; ++i) {
            SEXP k_elt = STRING_ELT(nms, i);
            if (k_elt == NA_STRING) {
                return false;
            }
            std::string key =
                cpp11::unwind_protect([&]() { return std::string(Rf_translateCharUTF8(k_elt)); });
            type_caster<remove_cvref_t<ValT>> vc;
            if (!vc.from_r(VECTOR_ELT(x, i), convert)) {
                return false;
            }
            val.emplace(std::move(key), std::move(vc.get()));
        }
        return true;
    }

    MapT& get() noexcept { return val; }

    static SEXP to_r(const MapT& m, rv_policy policy, SEXP parent) {
        const R_xlen_t n = static_cast<R_xlen_t>(m.size());
        SEXP out = PROTECT(cpp11::safe[Rf_allocVector](VECSXP, n));
        SEXP nms = PROTECT(cpp11::safe[Rf_allocVector](STRSXP, n));
        R_xlen_t idx = 0;
        for (const auto& [k, v] : m) {
            SET_VECTOR_ELT(out, idx, to_r_sexp(v, policy, parent));
            SEXP ch = cpp11::unwind_protect(
                [&]() { return Rf_mkCharLenCE(k.data(), static_cast<int>(k.size()), CE_UTF8); });
            SET_STRING_ELT(nms, idx, ch);
            ++idx;
        }
        Rf_setAttrib(out, R_NamesSymbol, nms);
        UNPROTECT(2);
        return out;
    }
};

template <typename V, typename Cmp, typename Alloc>
struct type_caster<std::map<std::string, V, Cmp, Alloc>>
    : string_map_caster<std::map<std::string, V, Cmp, Alloc>> {};

template <typename V, typename Hash, typename Eq, typename Alloc>
struct type_caster<std::unordered_map<std::string, V, Hash, Eq, Alloc>>
    : string_map_caster<std::unordered_map<std::string, V, Hash, Eq, Alloc>> {};

} // namespace nanorbind::r_detail
