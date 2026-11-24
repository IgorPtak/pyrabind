#pragma once

#include "pyrabind/r/caster.hpp"
#include "pyrabind/r/dispatcher.hpp"
#include "pyrabind/r/registry.hpp"
#include "pyrabind/r/traits.hpp"
#include "pyrabind/r/types.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <utility>

#if defined(_WIN32)
#define PYRABIND_R_EXPORT __declspec(dllexport)
#else
#define PYRABIND_R_EXPORT __attribute__((visibility("default")))
#endif

namespace pyrabind::r_detail {

// Exported R C-API entry points implemented in src/r/runtime.cpp & src/r/module.cpp.
SEXP r_create_module_env(const char* name, SEXP dispatch_env, void (*init_fn)(module_record&));

extern "C" {
PYRABIND_R_EXPORT SEXP pyrabind_ext_call_fn(SEXP args);
PYRABIND_R_EXPORT SEXP pyrabind_ext_construct(SEXP args);
PYRABIND_R_EXPORT SEXP pyrabind_ext_call_method(SEXP args);
PYRABIND_R_EXPORT SEXP pyrabind_call_get_prop(SEXP obj, SEXP prop_xptr);
PYRABIND_R_EXPORT SEXP pyrabind_call_set_prop(SEXP obj, SEXP prop_xptr, SEXP val);
PYRABIND_R_EXPORT SEXP pyrabind_call_op(SEXP op_sexp, SEXP lhs, SEXP rhs, SEXP unary_sexp);
PYRABIND_R_EXPORT SEXP pyrabind_call_special(SEXP obj, SEXP kind_sexp);
PYRABIND_R_EXPORT SEXP pyrabind_call_enum_op(SEXP op_sexp, SEXP a, SEXP b);
}

template <typename Derived, typename Base> struct single_base_extractor {
    static std::type_index type() noexcept { return std::type_index(typeid(Base)); }
    static void* (*upcast_fn() noexcept)(void*) noexcept {
        return [](void* p) noexcept -> void* {
            return static_cast<void*>(static_cast<Base*>(static_cast<Derived*>(p)));
        };
    }
};

class module_;
template <typename T, typename... Bases> class class_;
template <typename E> class enum_;

} // namespace pyrabind::r_detail

namespace pyrabind {

class module_ {
public:
    explicit module_(r_detail::module_record* rec = nullptr) noexcept : record_(rec) {}

    struct doc_proxy {
        std::string& target;
        doc_proxy& operator=(std::string_view s) {
            target = std::string(s);
            return *this;
        }
        operator const std::string&() const noexcept { return target; }
    };

    struct attr_proxy {
        r_detail::module_record* mod;
        std::string key;

        template <typename T> attr_proxy& operator=(T&& val) {
            using ValT = std::decay_t<T>;
            if constexpr (std::is_convertible_v<ValT, std::string_view>) {
                if (key == "__doc__") {
                    mod->doc = std::string(std::string_view(std::forward<T>(val)));
                    return *this;
                }
            }
            if constexpr (std::is_same_v<ValT, const char*> ||
                          std::is_array_v<std::remove_reference_t<T>>) {
                std::string stored(val);
                mod->constants.emplace_back(std::move(key), [stored = std::move(stored)]() -> SEXP {
                    return r_detail::to_r_sexp(stored);
                });
            } else {
                mod->constants.emplace_back(std::move(key),
                                            [stored = ValT(std::forward<T>(val))]() -> SEXP {
                                                return r_detail::to_r_sexp(stored);
                                            });
            }
            return *this;
        }
    };

    doc_proxy doc() noexcept { return doc_proxy{record_->doc}; }

    attr_proxy attr(const char* name) { return attr_proxy{record_, name != nullptr ? name : ""}; }

    template <typename Fn, typename... Extra>
    module_& def(const char* name, Fn&& fn, Extra&&... extra) {
        auto& fn_rec = record_->get_or_create_function(name);
        fn_rec.overloads.push_back(
            r_detail::make_function_overload(std::forward<Fn>(fn), std::forward<Extra>(extra)...));
        return *this;
    }

    module_ def_submodule(const char* name, const char* docstring = nullptr) {
        auto sub = std::make_unique<r_detail::module_record>();
        sub->name = name != nullptr ? name : "";
        sub->qualified_name = record_->qualified_name + "." + sub->name;
        if (docstring != nullptr) {
            sub->doc = docstring;
        }
        r_detail::module_record* raw = sub.get();
        record_->submodules.push_back(std::move(sub));
        return module_(raw);
    }

    r_detail::module_record* raw() const noexcept { return record_; }

private:
    r_detail::module_record* record_ = nullptr;
};

template <typename T, typename... Bases> class class_ {
    static_assert(sizeof...(Bases) <= 1, "pyrabind R backend supports at most one base class");

public:
    using Type = T;

    template <typename... Extra> class_(module_& mod, const char* name, Extra&&... extra) {
        init_class(mod.raw(), name, std::forward<Extra>(extra)...);
    }

    template <typename... Extra> class_(module_&& mod, const char* name, Extra&&... extra) {
        init_class(mod.raw(), name, std::forward<Extra>(extra)...);
    }

    template <typename Fn, typename... Extra>
    class_& def(const char* name, Fn&& fn, Extra&&... extra) {
        auto& fn_rec = record_->get_or_create_method(name, false);
        fn_rec.overloads.push_back(
            r_detail::make_method_overload<T>(std::forward<Fn>(fn), std::forward<Extra>(extra)...));
        return *this;
    }

    template <typename... CtorArgs, typename... Extra>
    class_& def(init<CtorArgs...> tag, Extra&&... extra) {
        record_->constructors.push_back(
            r_detail::make_constructor_overload<T>(tag, std::forward<Extra>(extra)...));
        return *this;
    }

    template <typename Fn, typename... Extra>
    class_& def(const new_factory<Fn>& nf, Extra&&... extra) {
        record_->constructors.push_back(
            r_detail::make_function_overload(nf.fn, std::forward<Extra>(extra)...));
        return *this;
    }

    template <typename OpExpr, typename... Extra>
    auto def(const OpExpr& op, Extra&&... extra)
        -> decltype(op.execute(*this, std::forward<Extra>(extra)...), *this) {
        op.execute(*this, std::forward<Extra>(extra)...);
        return *this;
    }

    template <typename Fn, typename... Extra>
    class_& def_static(const char* name, Fn&& fn, Extra&&... extra) {
        auto& fn_rec = record_->get_or_create_method(name, true);
        fn_rec.overloads.push_back(
            r_detail::make_function_overload(std::forward<Fn>(fn), std::forward<Extra>(extra)...));
        return *this;
    }

    template <typename C, typename D, typename... Extra>
    class_& def_ro(const char* name, D C::* pm, Extra&&... extra) {
        static_assert(std::is_base_of_v<C, T> || std::is_same_v<C, T>,
                      "member pointer does not belong to bound class or base");
        return def_prop_ro(
            name, [pm](const T& self) -> const D& { return self.*pm; },
            std::forward<Extra>(extra)...);
    }

    template <typename C, typename D, typename... Extra>
    class_& def_rw(const char* name, D C::* pm, Extra&&... extra) {
        static_assert(std::is_base_of_v<C, T> || std::is_same_v<C, T>,
                      "member pointer does not belong to bound class or base");
        return def_prop_rw(
            name, [pm](const T& self) -> const D& { return self.*pm; },
            [pm](T& self, D val) { self.*pm = std::move(val); }, std::forward<Extra>(extra)...);
    }

    template <typename Getter, typename Setter, typename... Extra>
    class_& def_prop_rw(const char* name, Getter&& getter, Setter&& setter, Extra&&... extra) {
        r_detail::overload_meta meta =
            r_detail::collect_meta(0, rv_policy::reference_internal, std::forward<Extra>(extra)...);
        r_detail::property_record prop;
        prop.name = name != nullptr ? name : "";
        prop.doc = std::move(meta.doc_str);
        const rv_policy pol = meta.policy;

        using DecayedG = std::decay_t<Getter>;
        prop.getter = [g = DecayedG(std::forward<Getter>(getter)),
                       pol](SEXP self_sexp) mutable -> SEXP {
            r_detail::type_caster<T> self_caster;
            if (!self_caster.from_r(self_sexp, false)) {
                throw type_error("invalid self object for property access");
            }
            using GTraits = r_detail::function_traits<DecayedG>;
            if constexpr (GTraits::is_member) {
                return r_detail::to_r_sexp((self_caster.get().*g)(), pol, self_sexp);
            } else {
                return r_detail::to_r_sexp(g(self_caster.get()), pol, self_sexp);
            }
        };

        if constexpr (!std::is_same_v<std::decay_t<Setter>, std::nullptr_t>) {
            using DecayedS = std::decay_t<Setter>;
            using STraits = r_detail::function_traits<DecayedS>;
            prop.setter = [s = DecayedS(std::forward<Setter>(setter))](SEXP self_sexp,
                                                                       SEXP val_sexp) mutable {
                r_detail::type_caster<T> self_caster;
                if (!self_caster.from_r(self_sexp, false)) {
                    throw type_error("invalid self object for property assignment");
                }
                if constexpr (STraits::is_member) {
                    using ValArg = std::tuple_element_t<0, typename STraits::args_tuple>;
                    r_detail::type_caster<r_detail::remove_cvref_t<ValArg>> vc;
                    if (!vc.from_r(val_sexp, true)) {
                        throw type_error("invalid value type for property assignment");
                    }
                    (self_caster.get().*s)(static_cast<ValArg>(vc.get()));
                } else {
                    using ValArg = std::tuple_element_t<1, typename STraits::args_tuple>;
                    r_detail::type_caster<r_detail::remove_cvref_t<ValArg>> vc;
                    if (!vc.from_r(val_sexp, true)) {
                        throw type_error("invalid value type for property assignment");
                    }
                    s(self_caster.get(), static_cast<ValArg>(vc.get()));
                }
            };
        }

        record_->properties.push_back(std::move(prop));
        return *this;
    }

    template <typename Getter, typename... Extra>
    class_& def_prop_ro(const char* name, Getter&& getter, Extra&&... extra) {
        return def_prop_rw(name, std::forward<Getter>(getter), nullptr,
                           std::forward<Extra>(extra)...);
    }

private:
    template <typename... Extra>
    void init_class(r_detail::module_record* mod, const char* name, Extra&&... extra) {
        mod->classes.emplace_back();
        record_ = &mod->classes.back();
        record_->name = name != nullptr ? name : "";
        record_->qualified_name = mod->qualified_name + "." + record_->name;
        record_->type = std::type_index(typeid(T));

        r_detail::overload_meta meta;
        (r_detail::apply_extra(meta, std::forward<Extra>(extra)), ...);
        record_->doc = std::move(meta.doc_str);

        if constexpr (sizeof...(Bases) == 1) {
            using Base = std::tuple_element_t<0, std::tuple<Bases...>>;
            using Ext = r_detail::single_base_extractor<T, Base>;
            record_->base_type = Ext::type();
            record_->upcast = Ext::upcast_fn();
        }

        r_detail::type_registry::instance().register_class(record_);
    }

    r_detail::class_record* record_ = nullptr;
};

template <typename E> class enum_ {
    static_assert(std::is_enum_v<E>, "pyrabind::enum_ requires an enum type");

public:
    template <typename... Extra> enum_(module_& mod, const char* name, Extra&&... extra) {
        init_enum(mod.raw(), name, std::forward<Extra>(extra)...);
    }

    template <typename... Extra> enum_(module_&& mod, const char* name, Extra&&... extra) {
        init_enum(mod.raw(), name, std::forward<Extra>(extra)...);
    }

    enum_& value(const char* name, E val, const char* docstring = nullptr) {
        r_detail::enum_entry ent;
        ent.name = name != nullptr ? name : "";
        ent.value = static_cast<std::int64_t>(val);
        if (docstring != nullptr) {
            ent.doc = docstring;
        }
        record_->entries.push_back(std::move(ent));
        return *this;
    }

    enum_& export_values() {
        record_->exported = true;
        return *this;
    }

private:
    template <typename... Extra>
    void init_enum(r_detail::module_record* mod, const char* name, Extra&&... extra) {
        mod->enums.emplace_back();
        record_ = &mod->enums.back();
        record_->name = name != nullptr ? name : "";
        record_->qualified_name = mod->qualified_name + "." + record_->name;
        record_->type = std::type_index(typeid(E));
        r_detail::overload_meta meta;
        (r_detail::apply_extra(meta, std::forward<Extra>(extra)), ...);
        record_->doc = std::move(meta.doc_str);
        r_detail::type_registry::instance().register_enum(record_);
    }

    r_detail::enum_record* record_ = nullptr;
};

} // namespace pyrabind

#include <R_ext/Rdynload.h>
#include <cstring>

namespace pyrabind::r_detail {

template <typename FnPtr> inline DL_FUNC as_dl_func(FnPtr fn) noexcept {
    static_assert(sizeof(FnPtr) == sizeof(DL_FUNC));
    DL_FUNC out = nullptr;
    std::memcpy(&out, &fn, sizeof(out));
    return out;
}

inline void register_pyrabind_r_routines(DllInfo* dll, DL_FUNC module_env_fn) {
    static R_CallMethodDef call_methods[] = {
        {"pyrabind_module_env", nullptr, 1},
        {"pyrabind_get_prop", nullptr, 2},
        {"pyrabind_set_prop", nullptr, 3},
        {"pyrabind_call_op", nullptr, 4},
        {"pyrabind_call_special", nullptr, 2},
        {"pyrabind_call_enum_op", nullptr, 3},
        {nullptr, nullptr, 0},
    };
    call_methods[0].fun = module_env_fn;
    call_methods[1].fun = as_dl_func(&pyrabind_call_get_prop);
    call_methods[2].fun = as_dl_func(&pyrabind_call_set_prop);
    call_methods[3].fun = as_dl_func(&pyrabind_call_op);
    call_methods[4].fun = as_dl_func(&pyrabind_call_special);
    call_methods[5].fun = as_dl_func(&pyrabind_call_enum_op);

    static R_ExternalMethodDef ext_methods[] = {
        {"pyrabind_ext_call_fn", nullptr, -1},
        {"pyrabind_ext_construct", nullptr, -1},
        {"pyrabind_ext_call_method", nullptr, -1},
        {nullptr, nullptr, 0},
    };
    ext_methods[0].fun = as_dl_func(&pyrabind_ext_call_fn);
    ext_methods[1].fun = as_dl_func(&pyrabind_ext_construct);
    ext_methods[2].fun = as_dl_func(&pyrabind_ext_call_method);

    R_registerRoutines(dll, nullptr, call_methods, nullptr, ext_methods);
    R_useDynamicSymbols(dll, FALSE);
}

} // namespace pyrabind::r_detail

#define PYRABIND_MODULE(name, var)                                                                 \
    static void pyrabind_init_##name(pyrabind::module_);                                           \
    static void pyrabind_init_record_##name(pyrabind::r_detail::module_record& rec) {              \
        pyrabind_init_##name(pyrabind::module_(&rec));                                             \
    }                                                                                              \
    extern "C" {                                                                                   \
    static SEXP pyrabind_module_env_##name(SEXP dispatch_env) {                                    \
        return pyrabind::r_detail::r_create_module_env(#name, dispatch_env,                        \
                                                       &pyrabind_init_record_##name);              \
    }                                                                                              \
    PYRABIND_R_EXPORT void R_init_##name(DllInfo* dll) {                                           \
        pyrabind::r_detail::register_pyrabind_r_routines(                                          \
            dll, pyrabind::r_detail::as_dl_func(&pyrabind_module_env_##name));                     \
    }                                                                                              \
    }                                                                                              \
    void pyrabind_init_##name(pyrabind::module_ var)
