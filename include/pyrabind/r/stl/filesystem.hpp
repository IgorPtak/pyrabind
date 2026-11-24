#pragma once

#include "pyrabind/r/caster.hpp"

#include <filesystem>
#include <string>

namespace pyrabind::r_detail {

template <> struct type_caster<std::filesystem::path> {
    std::filesystem::path val;

    bool from_r(SEXP x, bool convert) {
        std::string s;
        if (!r_to_string(x, convert, s)) {
            return false;
        }
        val = std::filesystem::path(std::move(s));
        return true;
    }

    std::filesystem::path& get() noexcept { return val; }

    static SEXP to_r(const std::filesystem::path& p, rv_policy, SEXP) {
        return r_from_string(p.string());
    }
};

} // namespace pyrabind::r_detail
