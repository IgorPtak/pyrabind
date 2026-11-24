#pragma once

#include "pyrabind/make_iterator.hpp"
#include "pyrabind/operators.hpp"
#include "pyrabind/r/backend.hpp"

#include <algorithm>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace pyrabind {

namespace r_detail {

inline std::size_t wrap_vec_index(std::ptrdiff_t i, std::size_t n) {
    if (i < 0) {
        i += static_cast<std::ptrdiff_t>(n);
    }
    if (i < 0 || static_cast<std::size_t>(i) >= n) {
        throw index_error("vector index out of range");
    }
    return static_cast<std::size_t>(i);
}

template <typename Vector, rv_policy::value Policy, typename... Extra>
inline class_<Vector> bind_vector_impl(module_& scope, const char* name, Extra&&... extra) {
    using ValueRef = decltype(*std::declval<typename Vector::iterator&>());
    using Value = std::decay_t<ValueRef>;

    auto cl =
        class_<Vector>(scope, name, std::forward<Extra>(extra)...)
            .def(init<>())
            .def("__len__", [](const Vector& v) { return v.size(); })
            .def("__bool__", [](const Vector& v) { return !v.empty(); })
            .def(
                "__iter__",
                [](Vector& v) { return make_iterator<Policy>(type<Vector>(), "Iterator", v); },
                keep_alive<0, 1>())
            .def(
                "__getitem__",
                [](Vector& v, std::ptrdiff_t i) -> ValueRef {
                    return v[wrap_vec_index(i, v.size())];
                },
                rv_policy::policy_tag<Policy>())
            .def("clear", [](Vector& v) { v.clear(); });

    if constexpr (std::is_copy_constructible_v<Value>) {
        cl.def(init<const Vector&>())
            .def("append", [](Vector& v, const Value& val) { v.push_back(val); })
            .def("insert",
                 [](Vector& v, std::ptrdiff_t i, const Value& val) {
                     if (i < 0) {
                         i += static_cast<std::ptrdiff_t>(v.size());
                     }
                     if (i < 0 || static_cast<std::size_t>(i) > v.size()) {
                         throw index_error("vector insert index out of range");
                     }
                     v.insert(v.begin() + i, val);
                 })
            .def(
                "pop",
                [](Vector& v, std::ptrdiff_t index) -> Value {
                    const std::size_t idx = wrap_vec_index(index, v.size());
                    Value out = std::move(v[idx]);
                    v.erase(v.begin() + static_cast<std::ptrdiff_t>(idx));
                    return out;
                },
                arg("index") = -1)
            .def("extend",
                 [](Vector& v, const Vector& src) {
                     if (&src == &v) {
                         const std::size_t n = v.size();
                         v.reserve(2 * n);
                         for (std::size_t i = 0; i < n; ++i) {
                             v.push_back(v[i]);
                         }
                     } else {
                         v.insert(v.end(), src.begin(), src.end());
                     }
                 })
            .def("__setitem__", [](Vector& v, std::ptrdiff_t i,
                                   const Value& val) { v[wrap_vec_index(i, v.size())] = val; })
            .def("__delitem__", [](Vector& v, std::ptrdiff_t i) {
                v.erase(v.begin() + static_cast<std::ptrdiff_t>(wrap_vec_index(i, v.size())));
            });
    }

    if constexpr (requires(const Value& a, const Value& b) { a == b; }) {
        cl.def(self == self)
            .def(self != self)
            .def("__contains__",
                 [](const Vector& v, const Value& val) {
                     return std::find(v.begin(), v.end(), val) != v.end();
                 })
            .def("count", [](const Vector& v,
                             const Value& val) { return std::count(v.begin(), v.end(), val); })
            .def("remove", [](Vector& v, const Value& val) {
                auto it = std::find(v.begin(), v.end(), val);
                if (it == v.end()) {
                    throw value_error("value not found in vector");
                }
                v.erase(it);
            });
    }

    return cl;
}

} // namespace r_detail

template <typename Vector, rv_policy::value Policy = rv_policy::automatic_reference_v,
          typename... Extra>
inline class_<Vector> bind_vector(module_& scope, const char* name, Extra&&... extra) {
    return r_detail::bind_vector_impl<Vector, Policy>(scope, name, std::forward<Extra>(extra)...);
}

template <typename Vector, rv_policy::value Policy = rv_policy::automatic_reference_v,
          typename... Extra>
inline class_<Vector> bind_vector(module_&& scope, const char* name, Extra&&... extra) {
    return r_detail::bind_vector_impl<Vector, Policy>(scope, name, std::forward<Extra>(extra)...);
}

} // namespace pyrabind
