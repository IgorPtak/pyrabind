#pragma once

#include "pyrabind/r/caster.hpp"
#include "pyrabind/r/stl/pair.hpp"

#include <iterator>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace pyrabind {

struct iterator {
    std::function<SEXP(bool&, SEXP)> next_fn;
};

namespace r_detail {

template <> struct type_caster<iterator> {
    iterator val;

    bool from_r(SEXP, bool) { return false; }
    iterator& get() noexcept { return val; }

    static SEXP to_r(const iterator& it, rv_policy, SEXP) { return r_wrap_iterator(it.next_fn); }
};

template <typename T> struct iter_remove_rvalue_ref {
    using type = T;
};
template <typename T> struct iter_remove_rvalue_ref<T&&> {
    using type = T;
};

template <typename Iterator> struct iter_access {
    using result_type = decltype(*std::declval<Iterator&>());
    result_type operator()(Iterator& it) const { return *it; }
};

template <typename Iterator> struct iter_key_access {
    using result_type =
        typename iter_remove_rvalue_ref<decltype(((*std::declval<Iterator&>()).first))>::type;
    result_type operator()(Iterator& it) const { return (*it).first; }
};

template <typename Iterator> struct iter_value_access {
    using result_type =
        typename iter_remove_rvalue_ref<decltype(((*std::declval<Iterator&>()).second))>::type;
    result_type operator()(Iterator& it) const { return (*it).second; }
};

template <typename Access, rv_policy::value Policy, typename Iterator, typename Sentinel,
          typename... Extra>
inline iterator make_iterator_impl(handle scope, const char* name, Iterator first, Sentinel last,
                                   Extra&&... extra) {
    (void)scope;
    (void)name;
    ((void)extra, ...);

    struct State {
        Iterator it;
        Sentinel end;
        bool first_or_done = true;
    };

    auto st = std::make_shared<State>(
        State{std::forward<Iterator>(first), std::forward<Sentinel>(last), true});

    iterator out;
    out.next_fn = [st](bool& done, SEXP iter_sexp) mutable -> SEXP {
        if (!st->first_or_done) {
            ++st->it;
        } else {
            st->first_or_done = false;
        }
        if (st->it == st->end) {
            st->first_or_done = true;
            done = true;
            return R_NilValue;
        }
        done = false;
        return to_r_sexp(Access()(st->it), rv_policy(Policy), iter_sexp);
    };
    return out;
}

} // namespace r_detail

template <rv_policy::value Policy = rv_policy::automatic_reference_v, typename Iterator,
          typename Sentinel, typename... Extra,
          typename = decltype(std::declval<Iterator>() == std::declval<Sentinel>())>
inline iterator make_iterator(handle scope, const char* name, Iterator first, Sentinel last,
                              Extra&&... extra) {
    return r_detail::make_iterator_impl<r_detail::iter_access<Iterator>, Policy>(
        scope, name, std::forward<Iterator>(first), std::forward<Sentinel>(last),
        std::forward<Extra>(extra)...);
}

template <rv_policy::value Policy = rv_policy::automatic_reference_v, typename Iterator,
          typename Sentinel, typename... Extra>
inline iterator make_key_iterator(handle scope, const char* name, Iterator first, Sentinel last,
                                  Extra&&... extra) {
    return r_detail::make_iterator_impl<r_detail::iter_key_access<Iterator>, Policy>(
        scope, name, std::forward<Iterator>(first), std::forward<Sentinel>(last),
        std::forward<Extra>(extra)...);
}

template <rv_policy::value Policy = rv_policy::automatic_reference_v, typename Iterator,
          typename Sentinel, typename... Extra>
inline iterator make_value_iterator(handle scope, const char* name, Iterator first, Sentinel last,
                                    Extra&&... extra) {
    return r_detail::make_iterator_impl<r_detail::iter_value_access<Iterator>, Policy>(
        scope, name, std::forward<Iterator>(first), std::forward<Sentinel>(last),
        std::forward<Extra>(extra)...);
}

template <rv_policy::value Policy = rv_policy::automatic_reference_v, typename Type,
          typename... Extra, typename = decltype(std::begin(std::declval<Type&>()))>
inline iterator make_iterator(handle scope, const char* name, Type& value, Extra&&... extra) {
    return make_iterator<Policy>(scope, name, std::begin(value), std::end(value),
                                 std::forward<Extra>(extra)...);
}

} // namespace pyrabind
