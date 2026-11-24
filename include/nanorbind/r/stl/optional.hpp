#pragma once

#include "nanorbind/r/caster.hpp"

#include <optional>

namespace nanorbind::r_detail {

template <typename T> struct type_caster<std::optional<T>> {
    std::optional<T> val;

    bool from_r(SEXP x, bool convert) {
        if (x == R_NilValue) {
            val.reset();
            return true;
        }
        type_caster<remove_cvref_t<T>> inner;
        if (!inner.from_r(x, convert)) {
            return false;
        }
        val = inner.get();
        return true;
    }

    std::optional<T>& get() noexcept { return val; }

    static SEXP to_r(const std::optional<T>& opt, rv_policy policy, SEXP parent) {
        if (!opt.has_value()) {
            return R_NilValue;
        }
        return to_r_sexp(*opt, policy, parent);
    }
};

} // namespace nanorbind::r_detail
