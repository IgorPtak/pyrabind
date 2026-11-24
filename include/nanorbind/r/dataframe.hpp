#pragma once

#include "nanorbind/dataframe_types.hpp"
#include "nanorbind/r/caster.hpp"
#include "nanorbind/r/traits.hpp"

namespace nanorbind::r_detail {

bool r_to_data_frame(SEXP x, bool convert, data_frame& out);
SEXP r_from_data_frame(const data_frame& df);

template <> struct is_custom_value_type<data_frame> : std::true_type {};

template <> struct type_caster<data_frame> {
    data_frame val;

    bool from_r(SEXP x, bool convert) { return r_to_data_frame(x, convert, val); }
    data_frame& get() noexcept { return val; }
    static SEXP to_r(const data_frame& df, rv_policy, SEXP) { return r_from_data_frame(df); }
};

} // namespace nanorbind::r_detail
