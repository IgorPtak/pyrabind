#pragma once

#include "pyrabind/r/caster.hpp"

#include <variant>

namespace pyrabind::r_detail {

template <typename... Ts> struct type_caster<std::variant<Ts...>> {
    std::variant<Ts...> val;

    bool from_r(SEXP x, bool convert) {
        if (try_alts<0>(x, false)) {
            return true;
        }
        if (convert) {
            return try_alts<0>(x, true);
        }
        return false;
    }

    std::variant<Ts...>& get() noexcept { return val; }

    static SEXP to_r(const std::variant<Ts...>& var, rv_policy policy, SEXP parent) {
        return std::visit(
            [&](const auto& inner) -> SEXP { return to_r_sexp(inner, policy, parent); }, var);
    }

private:
    template <std::size_t I> bool try_alts(SEXP x, bool allow_conv) {
        if constexpr (I >= sizeof...(Ts)) {
            return false;
        } else {
            using AltT = std::variant_alternative_t<I, std::variant<Ts...>>;
            type_caster<remove_cvref_t<AltT>> c;
            if (c.from_r(x, allow_conv)) {
                val = std::move(c.get());
                return true;
            }
            return try_alts<I + 1>(x, allow_conv);
        }
    }
};

} // namespace pyrabind::r_detail
