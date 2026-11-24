#pragma once

#include "pyrabind/r/caster.hpp"
#include "pyrabind/r/registry.hpp"
#include "pyrabind/r/traits.hpp"
#include "pyrabind/r/types.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace pyrabind::r_detail {

struct overload_meta {
    rv_policy policy = rv_policy::automatic;
    bool method_flag = false;
    bool operator_flag = false;
    std::size_t max_positional = static_cast<std::size_t>(-1);
    std::string doc_str;
    std::vector<arg_spec> args;
    std::vector<keep_alive_spec> keep_alives;
};

template <typename T> struct is_call_guard : std::false_type {};
template <typename... Gs> struct is_call_guard<call_guard<Gs...>> : std::true_type {
    using tuple_type = std::tuple<Gs...>;
};

template <typename... Ts> struct collect_call_guards;
template <> struct collect_call_guards<> {
    using type = std::tuple<>;
};
template <typename T, typename... Rest> struct collect_call_guards<T, Rest...> {
    using CleanT = remove_cvref_t<T>;
    using TailT = typename collect_call_guards<Rest...>::type;
    static constexpr auto pick() noexcept {
        if constexpr (is_call_guard<CleanT>::value) {
            return std::type_identity<decltype(std::tuple_cat(
                std::declval<typename is_call_guard<CleanT>::tuple_type>(),
                std::declval<TailT>()))>{};
        } else {
            return std::type_identity<TailT>{};
        }
    }
    using type = typename decltype(pick())::type;
};

template <typename GuardTuple, typename Fn> inline decltype(auto) invoke_with_guards(Fn&& fn) {
    if constexpr (std::tuple_size_v<GuardTuple> == 0) {
        return fn();
    } else {
        [[maybe_unused]] GuardTuple guards{};
        return fn();
    }
}

inline void apply_extra(overload_meta& meta, const rv_policy& p) noexcept {
    meta.policy = p;
}
template <rv_policy::value V>
inline void apply_extra(overload_meta& meta, rv_policy::policy_tag<V>) noexcept {
    meta.policy = rv_policy(V);
}
inline void apply_extra(overload_meta& meta, is_method) noexcept {
    meta.method_flag = true;
}
inline void apply_extra(overload_meta& meta, is_operator) noexcept {
    meta.operator_flag = true;
}
inline void apply_extra(overload_meta&, is_implicit) noexcept {}
inline void apply_extra(overload_meta&, is_arithmetic) noexcept {}
inline void apply_extra(overload_meta& meta, kw_only) noexcept {
    meta.max_positional = meta.args.size();
}
template <typename... Gs> inline void apply_extra(overload_meta&, call_guard<Gs...>) noexcept {}

inline void apply_extra(overload_meta& meta, const char* text) {
    if (text != nullptr) {
        meta.doc_str = text;
    }
}

inline void apply_extra(overload_meta& meta, const arg& a) {
    arg_spec spec;
    if (a.name != nullptr) {
        spec.name = a.name;
    }
    spec.convert = a.convert;
    spec.accepts_none = a.accepts_none;
    spec.has_default = false;
    meta.args.push_back(std::move(spec));
}

inline void apply_extra(overload_meta& meta, const arg_v& a) {
    arg_spec spec;
    if (a.name != nullptr) {
        spec.name = a.name;
    }
    spec.convert = a.convert;
    spec.accepts_none = a.accepts_none;
    spec.has_default = true;
    spec.default_fn = a.default_fn;
    meta.args.push_back(std::move(spec));
}

template <std::size_t Nurse, std::size_t Patient>
inline void apply_extra(overload_meta& meta, keep_alive<Nurse, Patient>) {
    meta.keep_alives.push_back(keep_alive_spec{Nurse, Patient});
}

template <typename... Extra>
inline overload_meta collect_meta(std::size_t param_count, Extra&&... extra) {
    overload_meta meta;
    (apply_extra(meta, std::forward<Extra>(extra)), ...);
    if (meta.args.size() > param_count) {
        throw value_error("too many nb::arg annotations supplied for callable arity");
    }
    const std::size_t missing_prefix = param_count - meta.args.size();
    if (missing_prefix > 0 && !meta.args.empty()) {
        std::vector<arg_spec> padded(missing_prefix);
        for (auto& a : meta.args) {
            padded.push_back(std::move(a));
        }
        meta.args = std::move(padded);
        if (meta.max_positional != static_cast<std::size_t>(-1)) {
            meta.max_positional += missing_prefix;
        }
    } else if (meta.args.empty() && param_count > 0) {
        meta.args.resize(param_count);
    }
    return meta;
}

template <std::size_t ParamCount>
inline bool resolve_arguments(const call_args& cargs, std::size_t pos_offset,
                              std::size_t max_positional, const std::vector<arg_spec>& specs,
                              std::array<SEXP, ParamCount>& resolved) {
    if (cargs.n_pos < pos_offset) {
        return false;
    }
    const std::size_t user_pos = cargs.n_pos - pos_offset;
    if (user_pos > ParamCount || user_pos > max_positional) {
        return false;
    }
    std::array<bool, ParamCount> filled{};
    for (std::size_t i = 0; i < user_pos; ++i) {
        resolved[i] = cargs.pos_at(pos_offset + i);
        filled[i] = true;
    }
    for (std::size_t k = 0; k < cargs.n_kw; ++k) {
        const std::string_view kname(cargs.kw_name_at(k));
        bool found = false;
        for (std::size_t p = 0; p < ParamCount; ++p) {
            if (p < specs.size() && specs[p].name == kname) {
                if (filled[p]) {
                    return false;
                }
                resolved[p] = cargs.kw_val_at(k);
                filled[p] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    for (std::size_t p = 0; p < ParamCount; ++p) {
        if (!filled[p]) {
            if (p < specs.size() && specs[p].has_default && specs[p].default_fn) {
                resolved[p] = specs[p].default_fn();
                filled[p] = true;
            } else {
                return false;
            }
        }
    }
    return true;
}

inline SEXP select_keep_alive_arg(const call_args& cargs, bool method_flag,
                                  const SEXP* resolved_args, std::size_t param_count,
                                  std::size_t index, SEXP ret_val) noexcept {
    if (index == 0) {
        return ret_val;
    }
    if (method_flag) {
        if (index == 1) {
            return cargs.n_pos > 0 ? cargs.pos_at(0) : R_NilValue;
        }
        const std::size_t p_idx = index - 2;
        return p_idx < param_count ? resolved_args[p_idx] : R_NilValue;
    }
    const std::size_t p_idx = index - 1;
    return p_idx < param_count ? resolved_args[p_idx] : R_NilValue;
}

inline void wire_keep_alives(const std::vector<keep_alive_spec>& specs, const call_args& cargs,
                             bool method_flag, const SEXP* resolved_args, std::size_t param_count,
                             SEXP ret_val) {
    for (const auto& ka : specs) {
        SEXP nurse = select_keep_alive_arg(cargs, method_flag, resolved_args, param_count, ka.nurse,
                                           ret_val);
        SEXP patient = select_keep_alive_arg(cargs, method_flag, resolved_args, param_count,
                                             ka.patient, ret_val);
        if (nurse != R_NilValue && patient != R_NilValue) {
            r_attach_keep_alive(nurse, patient);
        }
    }
}

template <typename Tuple, std::size_t... Is>
inline bool convert_tuple_args(
    const std::array<SEXP, sizeof...(Is)>& sexps, const std::vector<arg_spec>& specs,
    bool global_convert,
    std::tuple<type_caster<remove_cvref_t<std::tuple_element_t<Is, Tuple>>>...>& casters,
    std::index_sequence<Is...>) {
    return (([&]() -> bool {
                using RawParamT = std::tuple_element_t<Is, Tuple>;
                const bool allow_conv =
                    global_convert && (Is < specs.size() ? specs[Is].convert : true);
                if constexpr (std::is_pointer_v<RawParamT>) {
                    if (sexps[Is] == R_NilValue && Is < specs.size() && specs[Is].accepts_none) {
                        return std::get<Is>(casters).from_r(R_NilValue, allow_conv);
                    }
                }
                return std::get<Is>(casters).from_r(sexps[Is], allow_conv);
            })() &&
            ...);
}

template <typename Fn, typename ArgsTuple, std::size_t... Is, typename CastersTuple>
inline decltype(auto) invoke_with_casters(Fn& fn, CastersTuple& casters,
                                          std::index_sequence<Is...>) {
    return fn(static_cast<std::tuple_element_t<Is, ArgsTuple>&&>(std::get<Is>(casters).get())...);
}

template <typename Fn, typename Cls, typename ArgsTuple, std::size_t... Is, typename CastersTuple>
inline decltype(auto) invoke_method_with_casters(Fn& fn, Cls& self, CastersTuple& casters,
                                                 std::index_sequence<Is...>) {
    return (self.*
            fn)(static_cast<std::tuple_element_t<Is, ArgsTuple>&&>(std::get<Is>(casters).get())...);
}

template <typename Fn, typename... Extra>
inline overload_record make_function_overload(Fn&& fn, Extra&&... extra) {
    using DecayedFn = std::decay_t<Fn>;
    using Traits = function_traits<DecayedFn>;
    static_assert(!Traits::is_member, "use make_method_overload for member functions");
    using ArgsTuple = typename Traits::args_tuple;
    using RetT = typename Traits::return_type;
    using GuardTuple = typename collect_call_guards<Extra...>::type;
    constexpr std::size_t kArity = Traits::arity;

    overload_meta meta = collect_meta(kArity, std::forward<Extra>(extra)...);

    overload_record rec;
    rec.arity = kArity;
    rec.max_positional = meta.max_positional;
    rec.args = std::move(meta.args);
    rec.keep_alives = std::move(meta.keep_alives);
    rec.is_method = meta.method_flag;
    rec.is_operator = meta.operator_flag;
    rec.doc = std::move(meta.doc_str);

    const std::vector<arg_spec> specs_copy = rec.args;
    const std::vector<keep_alive_spec> ka_copy = rec.keep_alives;
    const rv_policy pol = meta.policy;
    const bool is_m = rec.is_method;
    const std::size_t max_pos = rec.max_positional;

    rec.invoke = [bound_fn = DecayedFn(std::forward<Fn>(fn)), specs_copy, ka_copy, pol, is_m,
                  max_pos](const call_args& cargs, bool convert, bool& matched,
                           SEXP parent) mutable -> SEXP {
        std::array<SEXP, (kArity > 0 ? kArity : 1)> resolved{};
        if constexpr (kArity == 0) {
            if (cargs.n_pos != 0 || cargs.n_kw != 0) {
                matched = false;
                return R_NilValue;
            }
            matched = true;
            if constexpr (std::is_void_v<RetT>) {
                (void)pol;
                (void)is_m;
                (void)max_pos;
                invoke_with_guards<GuardTuple>([&]() { bound_fn(); });
                return R_NilValue;
            } else {
                (void)max_pos;
                SEXP out = to_r_sexp(
                    invoke_with_guards<GuardTuple>([&]() -> decltype(auto) { return bound_fn(); }),
                    pol, parent);
                wire_keep_alives(ka_copy, cargs, is_m, nullptr, 0, out);
                return out;
            }
        } else {
            if (!resolve_arguments<kArity>(cargs, 0, max_pos, specs_copy, resolved)) {
                matched = false;
                return R_NilValue;
            }
            auto casters = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                return std::tuple<
                    type_caster<remove_cvref_t<std::tuple_element_t<Is, ArgsTuple>>>...>{};
            }(std::make_index_sequence<kArity>{});

            if (!convert_tuple_args<ArgsTuple>(resolved, specs_copy, convert, casters,
                                               std::make_index_sequence<kArity>{})) {
                matched = false;
                return R_NilValue;
            }
            matched = true;
            if constexpr (std::is_void_v<RetT>) {
                (void)pol;
                invoke_with_guards<GuardTuple>([&]() {
                    invoke_with_casters<DecayedFn, ArgsTuple>(bound_fn, casters,
                                                              std::make_index_sequence<kArity>{});
                });
                wire_keep_alives(ka_copy, cargs, is_m, resolved.data(), kArity, R_NilValue);
                return R_NilValue;
            } else {
                SEXP out = to_r_sexp(invoke_with_guards<GuardTuple>([&]() -> decltype(auto) {
                                         return invoke_with_casters<DecayedFn, ArgsTuple>(
                                             bound_fn, casters, std::make_index_sequence<kArity>{});
                                     }),
                                     pol, parent);
                wire_keep_alives(ka_copy, cargs, is_m, resolved.data(), kArity, out);
                return out;
            }
        }
    };
    return rec;
}

template <typename ClassT, typename Fn, typename... Extra>
inline overload_record make_method_overload(Fn&& fn, Extra&&... extra) {
    using DecayedFn = std::decay_t<Fn>;
    using Traits = function_traits<DecayedFn>;
    using GuardTuple = typename collect_call_guards<Extra...>::type;

    if constexpr (!Traits::is_member) {
        // Free function / lambda taking `ClassT&` / `const ClassT&` as its first argument.
        using FullTuple = typename Traits::args_tuple;
        static_assert(Traits::arity >= 1,
                      "callable method adapter requires self as its first parameter");
        using SelfParamT = std::tuple_element_t<0, FullTuple>;
        constexpr std::size_t kUserArity = Traits::arity - 1;
        overload_meta meta = collect_meta(kUserArity, std::forward<Extra>(extra)...);

        overload_record rec;
        rec.arity = kUserArity;
        rec.max_positional = meta.max_positional;
        rec.args = std::move(meta.args);
        rec.keep_alives = std::move(meta.keep_alives);
        rec.is_method = true;
        rec.is_operator = meta.operator_flag;
        rec.doc = std::move(meta.doc_str);

        const std::vector<arg_spec> specs_copy = rec.args;
        const std::vector<keep_alive_spec> ka_copy = rec.keep_alives;
        const rv_policy pol = meta.policy;
        const std::size_t max_pos = rec.max_positional;

        rec.invoke = [bound_fn = DecayedFn(std::forward<Fn>(fn)), specs_copy, ka_copy, pol,
                      max_pos](const call_args& cargs, bool convert, bool& matched,
                               SEXP parent) mutable -> SEXP {
            if (cargs.n_pos < 1) {
                matched = false;
                return R_NilValue;
            }
            type_caster<remove_cvref_t<SelfParamT>> self_caster;
            if (!self_caster.from_r(cargs.pos_at(0), false)) {
                matched = false;
                return R_NilValue;
            }

            if constexpr (kUserArity == 0) {
                if (cargs.n_pos != 1 || cargs.n_kw != 0) {
                    matched = false;
                    return R_NilValue;
                }
                matched = true;
                SEXP self_sexp = cargs.pos_at(0);
                SEXP ret_parent = parent != R_NilValue ? parent : self_sexp;
                using RetT = typename Traits::return_type;
                if constexpr (std::is_void_v<RetT>) {
                    (void)pol;
                    (void)ret_parent;
                    (void)max_pos;
                    invoke_with_guards<GuardTuple>(
                        [&]() { bound_fn(static_cast<SelfParamT&&>(self_caster.get())); });
                    wire_keep_alives(ka_copy, cargs, true, nullptr, 0, R_NilValue);
                    return R_NilValue;
                } else {
                    (void)max_pos;
                    SEXP out =
                        to_r_sexp(invoke_with_guards<GuardTuple>([&]() -> decltype(auto) {
                                      return bound_fn(static_cast<SelfParamT&&>(self_caster.get()));
                                  }),
                                  pol, ret_parent);
                    wire_keep_alives(ka_copy, cargs, true, nullptr, 0, out);
                    return out;
                }
            } else {
                auto build_user_tuple = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                    return std::type_identity<
                        std::tuple<std::tuple_element_t<Is + 1, FullTuple>...>>{};
                };
                using UserTuple = typename decltype(build_user_tuple(
                    std::make_index_sequence<kUserArity>{}))::type;

                std::array<SEXP, kUserArity> resolved{};
                if (!resolve_arguments<kUserArity>(cargs, 1, max_pos, specs_copy, resolved)) {
                    matched = false;
                    return R_NilValue;
                }
                auto casters = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                    return std::tuple<
                        type_caster<remove_cvref_t<std::tuple_element_t<Is, UserTuple>>>...>{};
                }(std::make_index_sequence<kUserArity>{});

                if (!convert_tuple_args<UserTuple>(resolved, specs_copy, convert, casters,
                                                   std::make_index_sequence<kUserArity>{})) {
                    matched = false;
                    return R_NilValue;
                }
                matched = true;
                SEXP self_sexp = cargs.pos_at(0);
                SEXP ret_parent = parent != R_NilValue ? parent : self_sexp;
                using RetT = typename Traits::return_type;
                auto call_it =
                    [&]<std::size_t... Is>(std::index_sequence<Is...>) -> decltype(auto) {
                    return bound_fn(static_cast<SelfParamT&&>(self_caster.get()),
                                    static_cast<std::tuple_element_t<Is, UserTuple>&&>(
                                        std::get<Is>(casters).get())...);
                };
                if constexpr (std::is_void_v<RetT>) {
                    (void)pol;
                    (void)ret_parent;
                    invoke_with_guards<GuardTuple>(
                        [&]() { call_it(std::make_index_sequence<kUserArity>{}); });
                    wire_keep_alives(ka_copy, cargs, true, resolved.data(), kUserArity, R_NilValue);
                    return R_NilValue;
                } else {
                    SEXP out = to_r_sexp(invoke_with_guards<GuardTuple>([&]() -> decltype(auto) {
                                             return call_it(std::make_index_sequence<kUserArity>{});
                                         }),
                                         pol, ret_parent);
                    wire_keep_alives(ka_copy, cargs, true, resolved.data(), kUserArity, out);
                    return out;
                }
            }
        };
        return rec;
    } else {
        using OwnerT = typename Traits::class_type;
        static_assert(std::is_base_of_v<OwnerT, ClassT> || std::is_same_v<OwnerT, ClassT>,
                      "member function does not belong to bound class or base class");
        using ArgsTuple = typename Traits::args_tuple;
        using RetT = typename Traits::return_type;
        constexpr std::size_t kArity = Traits::arity;

        overload_meta meta = collect_meta(kArity, std::forward<Extra>(extra)...);
        overload_record rec;
        rec.arity = kArity;
        rec.max_positional = meta.max_positional;
        rec.args = std::move(meta.args);
        rec.keep_alives = std::move(meta.keep_alives);
        rec.is_method = true;
        rec.is_operator = meta.operator_flag;
        rec.doc = std::move(meta.doc_str);

        const std::vector<arg_spec> specs_copy = rec.args;
        const std::vector<keep_alive_spec> ka_copy = rec.keep_alives;
        const rv_policy pol = meta.policy;
        const std::size_t max_pos = rec.max_positional;

        rec.invoke = [bound_fn = DecayedFn(std::forward<Fn>(fn)), specs_copy, ka_copy, pol,
                      max_pos](const call_args& cargs, bool convert, bool& matched,
                               SEXP parent) mutable -> SEXP {
            if (cargs.n_pos < 1) {
                matched = false;
                return R_NilValue;
            }
            type_caster<ClassT> self_caster;
            if (!self_caster.from_r(cargs.pos_at(0), false)) {
                matched = false;
                return R_NilValue;
            }
            ClassT& self = self_caster.get();
            SEXP self_sexp = cargs.pos_at(0);
            SEXP ret_parent = parent != R_NilValue ? parent : self_sexp;

            if constexpr (kArity == 0) {
                if (cargs.n_pos != 1 || cargs.n_kw != 0) {
                    matched = false;
                    return R_NilValue;
                }
                matched = true;
                if constexpr (std::is_void_v<RetT>) {
                    (void)pol;
                    (void)ret_parent;
                    (void)max_pos;
                    invoke_with_guards<GuardTuple>([&]() { (self.*bound_fn)(); });
                    wire_keep_alives(ka_copy, cargs, true, nullptr, 0, R_NilValue);
                    return R_NilValue;
                } else {
                    (void)max_pos;
                    SEXP out = to_r_sexp(invoke_with_guards<GuardTuple>([&]() -> decltype(auto) {
                                             return (self.*bound_fn)();
                                         }),
                                         pol, ret_parent);
                    wire_keep_alives(ka_copy, cargs, true, nullptr, 0, out);
                    return out;
                }
            } else {
                std::array<SEXP, kArity> resolved{};
                if (!resolve_arguments<kArity>(cargs, 1, max_pos, specs_copy, resolved)) {
                    matched = false;
                    return R_NilValue;
                }
                auto casters = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                    return std::tuple<
                        type_caster<remove_cvref_t<std::tuple_element_t<Is, ArgsTuple>>>...>{};
                }(std::make_index_sequence<kArity>{});

                if (!convert_tuple_args<ArgsTuple>(resolved, specs_copy, convert, casters,
                                                   std::make_index_sequence<kArity>{})) {
                    matched = false;
                    return R_NilValue;
                }
                matched = true;
                if constexpr (std::is_void_v<RetT>) {
                    (void)pol;
                    (void)ret_parent;
                    invoke_with_guards<GuardTuple>([&]() {
                        invoke_method_with_casters<DecayedFn, ClassT, ArgsTuple>(
                            bound_fn, self, casters, std::make_index_sequence<kArity>{});
                    });
                    wire_keep_alives(ka_copy, cargs, true, resolved.data(), kArity, R_NilValue);
                    return R_NilValue;
                } else {
                    SEXP out = to_r_sexp(
                        invoke_with_guards<GuardTuple>([&]() -> decltype(auto) {
                            return invoke_method_with_casters<DecayedFn, ClassT, ArgsTuple>(
                                bound_fn, self, casters, std::make_index_sequence<kArity>{});
                        }),
                        pol, ret_parent);
                    wire_keep_alives(ka_copy, cargs, true, resolved.data(), kArity, out);
                    return out;
                }
            }
        };
        return rec;
    }
}

template <typename ClassT, typename... CtorArgs, typename... Extra>
inline overload_record make_constructor_overload(init<CtorArgs...>, Extra&&... extra) {
    using ArgsTuple = std::tuple<CtorArgs...>;
    using GuardTuple = typename collect_call_guards<Extra...>::type;
    constexpr std::size_t kArity = sizeof...(CtorArgs);
    overload_meta meta = collect_meta(kArity, std::forward<Extra>(extra)...);

    overload_record rec;
    rec.arity = kArity;
    rec.max_positional = meta.max_positional;
    rec.args = std::move(meta.args);
    rec.keep_alives = std::move(meta.keep_alives);
    rec.doc = std::move(meta.doc_str);

    const std::vector<arg_spec> specs_copy = rec.args;
    const std::vector<keep_alive_spec> ka_copy = rec.keep_alives;
    const std::size_t max_pos = rec.max_positional;

    rec.invoke = [specs_copy, ka_copy, max_pos](const call_args& cargs, bool convert, bool& matched,
                                                SEXP) mutable -> SEXP {
        const class_record* cls =
            type_registry::instance().find_class(std::type_index(typeid(ClassT)));
        if (cls == nullptr) {
            throw type_error("unregistered class in constructor");
        }
        std::array<SEXP, (kArity > 0 ? kArity : 1)> resolved{};
        if constexpr (kArity == 0) {
            if (cargs.n_pos != 0 || cargs.n_kw != 0) {
                matched = false;
                return R_NilValue;
            }
            matched = true;
            (void)max_pos;
            ClassT* raw = invoke_with_guards<GuardTuple>([]() { return new ClassT(); });
            instance_box box = make_owned_instance_box(raw);
            SEXP out = r_wrap_instance(std::move(box), cls, rv_policy::take_ownership, R_NilValue);
            wire_keep_alives(ka_copy, cargs, false, nullptr, 0, out);
            return out;
        } else {
            if (!resolve_arguments<kArity>(cargs, 0, max_pos, specs_copy, resolved)) {
                matched = false;
                return R_NilValue;
            }
            auto casters = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                return std::tuple<
                    type_caster<remove_cvref_t<std::tuple_element_t<Is, ArgsTuple>>>...>{};
            }(std::make_index_sequence<kArity>{});

            if (!convert_tuple_args<ArgsTuple>(resolved, specs_copy, convert, casters,
                                               std::make_index_sequence<kArity>{})) {
                matched = false;
                return R_NilValue;
            }
            matched = true;
            ClassT* raw = invoke_with_guards<GuardTuple>([&]() {
                return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                    return new ClassT(static_cast<std::tuple_element_t<Is, ArgsTuple>&&>(
                        std::get<Is>(casters).get())...);
                }(std::make_index_sequence<kArity>{});
            });

            instance_box box = make_owned_instance_box(raw);
            SEXP out = r_wrap_instance(std::move(box), cls, rv_policy::take_ownership, R_NilValue);
            wire_keep_alives(ka_copy, cargs, false, resolved.data(), kArity, out);
            return out;
        }
    };
    return rec;
}

} // namespace pyrabind::r_detail
