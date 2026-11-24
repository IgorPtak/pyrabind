#pragma once

#include <tuple>
#include <type_traits>

namespace nanorbind::r_detail {

template <typename T> using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename T> struct is_custom_value_type : std::false_type {};

template <typename T> inline constexpr bool is_custom_value_type_v = is_custom_value_type<T>::value;

template <typename T> struct function_traits;

template <typename R, typename... Args> struct function_traits<R(Args...)> {
    using return_type = R;
    using args_tuple = std::tuple<Args...>;
    static constexpr std::size_t arity = sizeof...(Args);
    static constexpr bool is_member = false;
};

template <typename R, typename... Args>
struct function_traits<R (*)(Args...)> : function_traits<R(Args...)> {};

template <typename R, typename... Args>
struct function_traits<R (*)(Args...) noexcept> : function_traits<R(Args...)> {};

template <typename R, typename C, typename... Args> struct function_traits<R (C::*)(Args...)> {
    using return_type = R;
    using class_type = C;
    using args_tuple = std::tuple<Args...>;
    static constexpr std::size_t arity = sizeof...(Args);
    static constexpr bool is_member = true;
    static constexpr bool is_const = false;
};

template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...) const> : function_traits<R (C::*)(Args...)> {
    static constexpr bool is_const = true;
};

template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...) noexcept> : function_traits<R (C::*)(Args...)> {};

template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...) const noexcept>
    : function_traits<R (C::*)(Args...) const> {};

template <typename Functor>
struct function_traits : function_traits<decltype(&std::decay_t<Functor>::operator())> {
    static constexpr bool is_member = false;
};

} // namespace nanorbind::r_detail
