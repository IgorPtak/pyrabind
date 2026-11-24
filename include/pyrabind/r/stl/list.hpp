#pragma once

#include "pyrabind/r/caster.hpp"
#include "pyrabind/r/stl/vector.hpp"

#include <list>
#include <utility>
#include <vector>

namespace pyrabind::r_detail {

template <typename T, typename Alloc> struct type_caster<std::list<T, Alloc>> {
    std::list<T, Alloc> val;

    bool from_r(SEXP x, bool convert) {
        val.clear();
        type_caster<std::vector<T>> vc;
        if (!vc.from_r(x, convert)) {
            return false;
        }
        for (auto& e : vc.get()) {
            val.push_back(std::move(e));
        }
        return true;
    }

    std::list<T, Alloc>& get() noexcept { return val; }

    static SEXP to_r(const std::list<T, Alloc>& lst, rv_policy policy, SEXP parent) {
        std::vector<T> tmp(lst.begin(), lst.end());
        return type_caster<std::vector<T>>::to_r(tmp, policy, parent);
    }
};

} // namespace pyrabind::r_detail
