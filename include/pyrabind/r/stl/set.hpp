#pragma once

#include "pyrabind/r/caster.hpp"
#include "pyrabind/r/stl/vector.hpp"

#include <set>
#include <unordered_set>
#include <vector>

namespace pyrabind::r_detail {

template <typename SetT> struct set_caster {
    SetT val;

    bool from_r(SEXP x, bool convert) {
        val.clear();
        using ElemT = typename SetT::value_type;
        type_caster<std::vector<ElemT>> vc;
        if (!vc.from_r(x, convert)) {
            return false;
        }
        for (auto& e : vc.get()) {
            val.insert(std::move(e));
        }
        return true;
    }

    SetT& get() noexcept { return val; }

    static SEXP to_r(const SetT& s, rv_policy policy, SEXP parent) {
        using ElemT = typename SetT::value_type;
        std::vector<ElemT> tmp(s.begin(), s.end());
        return type_caster<std::vector<ElemT>>::to_r(tmp, policy, parent);
    }
};

template <typename T, typename Cmp, typename Alloc>
struct type_caster<std::set<T, Cmp, Alloc>> : set_caster<std::set<T, Cmp, Alloc>> {};

template <typename T, typename Hash, typename Eq, typename Alloc>
struct type_caster<std::unordered_set<T, Hash, Eq, Alloc>>
    : set_caster<std::unordered_set<T, Hash, Eq, Alloc>> {};

} // namespace pyrabind::r_detail
