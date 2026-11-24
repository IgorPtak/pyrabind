#pragma once

#include "pyrabind/make_iterator.hpp"
#include "pyrabind/operators.hpp"
#include "pyrabind/r/backend.hpp"

#include <type_traits>
#include <utility>

namespace pyrabind {

namespace r_detail {

template <typename Map, typename Key, typename Value>
inline void map_assign(Map& m, const Key& k, const Value& v) {
    if constexpr (std::is_copy_assignable_v<Value>) {
        m[k] = v;
    } else {
        auto r = m.emplace(k, v);
        if (!r.second) {
            m.erase(r.first);
            m.emplace(k, v);
        }
    }
}

template <typename Map, rv_policy::value Policy, typename... Extra>
inline class_<Map> bind_map_impl(module_& scope, const char* name, Extra&&... extra) {
    using Key = typename Map::key_type;
    using Value = typename Map::mapped_type;
    using ValueRef = typename iter_remove_rvalue_ref<decltype((
        (*std::declval<typename Map::iterator&>()).second))>::type;

    auto cl =
        class_<Map>(scope, name, std::forward<Extra>(extra)...)
            .def(init<>())
            .def("__len__", [](const Map& m) { return m.size(); })
            .def("__bool__", [](const Map& m) { return !m.empty(); })
            .def("__contains__", [](const Map& m, const Key& k) { return m.find(k) != m.end(); })
            .def(
                "__getitem__",
                [](Map& m, const Key& k) -> ValueRef {
                    auto it = m.find(k);
                    if (it == m.end()) {
                        throw key_error("key not found in map");
                    }
                    return (*it).second;
                },
                rv_policy::policy_tag<Policy>())
            .def("__delitem__",
                 [](Map& m, const Key& k) {
                     auto it = m.find(k);
                     if (it == m.end()) {
                         throw key_error("key not found in map");
                     }
                     m.erase(it);
                 })
            .def("clear", [](Map& m) { m.clear(); })
            .def(
                "__iter__",
                [](Map& m) {
                    return make_key_iterator<Policy>(type<Map>(), "KeyIterator", m.begin(),
                                                     m.end());
                },
                keep_alive<0, 1>())
            .def(
                "keys",
                [](Map& m) {
                    return make_key_iterator<Policy>(type<Map>(), "KeyIterator", m.begin(),
                                                     m.end());
                },
                keep_alive<0, 1>())
            .def(
                "values",
                [](Map& m) {
                    return make_value_iterator<Policy>(type<Map>(), "ValueIterator", m.begin(),
                                                       m.end());
                },
                keep_alive<0, 1>())
            .def(
                "items",
                [](Map& m) {
                    return make_iterator<Policy>(type<Map>(), "ItemIterator", m.begin(), m.end());
                },
                keep_alive<0, 1>());

    if constexpr (std::is_copy_constructible_v<Map>) {
        cl.def(init<const Map&>());
    }

    if constexpr (std::is_copy_assignable_v<Value> || std::is_copy_constructible_v<Value>) {
        cl.def("__setitem__", [](Map& m, const Key& k, const Value& v) {
              map_assign(m, k, v);
          }).def("update", [](Map& m, const Map& m2) {
            if (&m2 == &m) {
                return;
            }
            for (const auto& kv : m2) {
                map_assign(m, kv.first, kv.second);
            }
        });
    }

    if constexpr (requires(const Map& a, const Map& b) { a == b; }) {
        cl.def(self == self).def(self != self);
    }

    return cl;
}

} // namespace r_detail

template <typename Map, rv_policy::value Policy = rv_policy::automatic_reference_v,
          typename... Extra>
inline class_<Map> bind_map(module_& scope, const char* name, Extra&&... extra) {
    return r_detail::bind_map_impl<Map, Policy>(scope, name, std::forward<Extra>(extra)...);
}

template <typename Map, rv_policy::value Policy = rv_policy::automatic_reference_v,
          typename... Extra>
inline class_<Map> bind_map(module_&& scope, const char* name, Extra&&... extra) {
    return r_detail::bind_map_impl<Map, Policy>(scope, name, std::forward<Extra>(extra)...);
}

} // namespace pyrabind
