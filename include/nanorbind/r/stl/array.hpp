#pragma once

#include "nanorbind/r/caster.hpp"
#include "nanorbind/r/stl/vector.hpp"

#include <array>
#include <cstddef>

namespace nanorbind::r_detail {

template <typename T, std::size_t N> struct type_caster<std::array<T, N>> {
    std::array<T, N> val{};

    bool from_r(SEXP x, bool convert) {
        type_caster<std::vector<T>> vc;
        if (!vc.from_r(x, convert) || vc.get().size() != N) {
            return false;
        }
        for (std::size_t i = 0; i < N; ++i) {
            val[i] = std::move(vc.get()[i]);
        }
        return true;
    }

    std::array<T, N>& get() noexcept { return val; }

    static SEXP to_r(const std::array<T, N>& arr, rv_policy policy, SEXP parent) {
        std::vector<T> tmp(arr.begin(), arr.end());
        return type_caster<std::vector<T>>::to_r(tmp, policy, parent);
    }
};

} // namespace nanorbind::r_detail
