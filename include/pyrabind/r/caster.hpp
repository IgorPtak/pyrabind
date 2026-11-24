#pragma once

#include "pyrabind/r/registry.hpp"
#include "pyrabind/r/traits.hpp"
#include "pyrabind/r/types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>

namespace pyrabind::r_detail {

// Compiled conversion hooks implemented in src/r/conversions.cpp.
bool r_to_bool(SEXP x, bool convert, bool& out);
bool r_to_i64(SEXP x, bool convert, std::int64_t& out);
bool r_to_u64(SEXP x, bool convert, std::uint64_t& out);
bool r_to_double(SEXP x, bool convert, double& out);
bool r_to_complex(SEXP x, bool convert, double& re, double& im);
bool r_to_string(SEXP x, bool convert, std::string& out);
bool r_to_cstr(SEXP x, bool convert, const char*& out);
bool r_to_duration_secs(SEXP x, bool convert, double& out_secs);
bool r_to_posix_secs(SEXP x, double& out_epoch_secs);

SEXP r_from_bool(bool v);
SEXP r_from_i64(std::int64_t v);
SEXP r_from_u64(std::uint64_t v);
SEXP r_from_double(double v);
SEXP r_from_complex(double re, double im);
SEXP r_from_string(std::string_view v);
SEXP r_from_duration_secs(double secs);
SEXP r_from_posix_secs(double epoch_secs);

bool r_extract_instance(SEXP x, std::type_index target_type, void*& out_ptr);
bool r_extract_shared_holder(SEXP x, std::type_index target_type, void*& out_ptr,
                             std::shared_ptr<void>& out_owner);
bool r_relinquish_unique_ptr(SEXP x, std::type_index target_type, bool poly_or_vd, void*& out_ptr);
SEXP r_wrap_instance(instance_box box, const class_record* cls, rv_policy policy, SEXP parent);

bool r_extract_enum(SEXP x, std::type_index enum_type, std::int64_t& out_val);
SEXP r_wrap_enum(const enum_record* rec, std::int64_t val);

SEXP r_wrap_capsule(const capsule& cap);
bool r_extract_capsule(SEXP x, capsule& out);

SEXP r_wrap_iterator(std::function<SEXP(bool&, SEXP)> next_fn);

void r_attach_keep_alive(SEXP nurse, SEXP patient);

// Shared implementation for bound C++ classes and PYRABIND_MAKE_OPAQUE types.
template <typename T> struct class_type_caster {
    T* ptr = nullptr;

    bool from_r(SEXP x, bool) {
        void* raw = nullptr;
        if (!r_extract_instance(x, std::type_index(typeid(T)), raw)) {
            return false;
        }
        ptr = static_cast<T*>(raw);
        return ptr != nullptr;
    }

    T& get() const {
        if (ptr == nullptr) {
            throw type_error("null C++ instance reference");
        }
        return *ptr;
    }

    template <typename U> static SEXP to_r(U&& v, rv_policy policy, SEXP parent) {
        using CleanU = remove_cvref_t<U>;
        const class_record* cls =
            type_registry::instance().find_class(std::type_index(typeid(CleanU)));
        if (cls == nullptr) {
            throw type_error("cannot return unregistered C++ type to R");
        }
        if constexpr (std::is_lvalue_reference_v<U> &&
                      !std::is_const_v<std::remove_reference_t<U>>) {
            if (policy == rv_policy::reference || policy == rv_policy::reference_internal ||
                policy == rv_policy::automatic_reference) {
                instance_box box;
                box.ptr = std::addressof(v);
                box.type = std::type_index(typeid(CleanU));
                box.owned = false;
                return r_wrap_instance(std::move(box), cls, policy, parent);
            }
        } else if constexpr (std::is_lvalue_reference_v<U>) {
            if (policy == rv_policy::reference || policy == rv_policy::reference_internal) {
                instance_box box;
                box.ptr = const_cast<CleanU*>(std::addressof(v));
                box.type = std::type_index(typeid(CleanU));
                box.owned = false;
                return r_wrap_instance(std::move(box), cls, policy, parent);
            }
        }
        if constexpr (std::is_copy_constructible_v<CleanU> ||
                      std::is_move_constructible_v<CleanU>) {
            auto* raw = new CleanU(std::forward<U>(v));
            instance_box box = make_owned_instance_box(raw);
            return r_wrap_instance(std::move(box), cls, rv_policy::move, parent);
        } else {
            throw type_error("cannot copy or move non-copyable C++ type to R");
        }
    }
};

// Primary template handles bound C++ classes; specializations take precedence automatically.
template <typename T, typename Enable = void> struct type_caster : class_type_caster<T> {};

template <> struct type_caster<handle> {
    handle val;
    bool from_r(SEXP x, bool) {
        val = handle(x);
        return true;
    }
    handle& get() noexcept { return val; }
    static SEXP to_r(handle h, rv_policy, SEXP) { return h.ptr(); }
};

template <typename T> struct type_caster<T, std::enable_if_t<std::is_same_v<T, bool>>> {
    bool val = false;
    bool from_r(SEXP x, bool convert) { return r_to_bool(x, convert, val); }
    bool& get() noexcept { return val; }
    static SEXP to_r(bool v, rv_policy, SEXP) { return r_from_bool(v); }
};

template <typename T>
struct type_caster<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>> {
    T val = 0;

    bool from_r(SEXP x, bool convert) {
        if constexpr (std::is_signed_v<T>) {
            std::int64_t tmp = 0;
            if (!r_to_i64(x, convert, tmp)) {
                return false;
            }
            val = static_cast<T>(tmp);
            return static_cast<std::int64_t>(val) == tmp;
        } else {
            std::uint64_t tmp = 0;
            if (!r_to_u64(x, convert, tmp)) {
                return false;
            }
            val = static_cast<T>(tmp);
            return static_cast<std::uint64_t>(val) == tmp;
        }
    }

    T& get() noexcept { return val; }

    static SEXP to_r(T v, rv_policy, SEXP) {
        if constexpr (std::is_signed_v<T>) {
            return r_from_i64(static_cast<std::int64_t>(v));
        } else {
            return r_from_u64(static_cast<std::uint64_t>(v));
        }
    }
};

template <typename T> struct type_caster<T, std::enable_if_t<std::is_floating_point_v<T>>> {
    T val = 0;
    bool from_r(SEXP x, bool convert) {
        double tmp = 0.0;
        if (!r_to_double(x, convert, tmp)) {
            return false;
        }
        val = static_cast<T>(tmp);
        return true;
    }
    T& get() noexcept { return val; }
    static SEXP to_r(T v, rv_policy, SEXP) { return r_from_double(static_cast<double>(v)); }
};

template <> struct type_caster<std::string> {
    std::string val;
    bool from_r(SEXP x, bool convert) { return r_to_string(x, convert, val); }
    std::string& get() noexcept { return val; }
    static SEXP to_r(std::string_view v, rv_policy, SEXP) { return r_from_string(v); }
};

template <> struct type_caster<std::string_view> {
    std::string storage;
    std::string_view val;
    bool from_r(SEXP x, bool convert) {
        if (!r_to_string(x, convert, storage)) {
            return false;
        }
        val = storage;
        return true;
    }
    std::string_view& get() noexcept { return val; }
    static SEXP to_r(std::string_view v, rv_policy, SEXP) { return r_from_string(v); }
};

template <> struct type_caster<const char*> {
    const char* val = nullptr;
    bool from_r(SEXP x, bool convert) { return r_to_cstr(x, convert, val); }
    const char*& get() noexcept { return val; }
    static SEXP to_r(const char* v, rv_policy, SEXP) {
        return v == nullptr ? R_NilValue : r_from_string(v);
    }
};

template <> struct type_caster<capsule> {
    capsule val;
    bool from_r(SEXP x, bool) { return r_extract_capsule(x, val); }
    capsule& get() noexcept { return val; }
    static SEXP to_r(const capsule& c, rv_policy, SEXP) { return r_wrap_capsule(c); }
};

template <typename E> struct type_caster<E, std::enable_if_t<std::is_enum_v<E>>> {
    E val{};

    bool from_r(SEXP x, bool convert) {
        std::int64_t raw = 0;
        if (r_extract_enum(x, std::type_index(typeid(E)), raw)) {
            val = static_cast<E>(raw);
            return true;
        }
        if (convert && r_to_i64(x, false, raw)) {
            val = static_cast<E>(raw);
            return true;
        }
        return false;
    }

    E& get() noexcept { return val; }

    static SEXP to_r(E v, rv_policy, SEXP) {
        const enum_record* rec = type_registry::instance().find_enum(std::type_index(typeid(E)));
        if (rec == nullptr) {
            return r_from_i64(static_cast<std::int64_t>(v));
        }
        return r_wrap_enum(rec, static_cast<std::int64_t>(v));
    }
};

template <typename Ptr>
struct type_caster<Ptr,
                   std::enable_if_t<std::is_pointer_v<Ptr> && !std::is_same_v<Ptr, const char*>>> {
    using BaseT = std::remove_cv_t<std::remove_pointer_t<Ptr>>;
    BaseT* val = nullptr;

    bool from_r(SEXP x, bool) {
        if (x == R_NilValue) {
            val = nullptr;
            return true;
        }
        void* raw = nullptr;
        if (!r_extract_instance(x, std::type_index(typeid(BaseT)), raw)) {
            return false;
        }
        val = static_cast<BaseT*>(raw);
        return true;
    }

    Ptr get() const noexcept { return static_cast<Ptr>(val); }

    static SEXP to_r(Ptr p, rv_policy policy, SEXP parent) {
        if (p == nullptr) {
            return R_NilValue;
        }
        const class_record* cls =
            type_registry::instance().find_class(std::type_index(typeid(BaseT)));
        if (cls == nullptr) {
            throw type_error("cannot return pointer to unregistered C++ type");
        }
        auto* raw = const_cast<BaseT*>(p);
        if (policy == rv_policy::take_ownership || policy == rv_policy::automatic) {
            instance_box box = make_owned_instance_box(raw);
            return r_wrap_instance(std::move(box), cls, rv_policy::take_ownership, parent);
        } else if (policy == rv_policy::copy) {
            if constexpr (std::is_copy_constructible_v<BaseT>) {
                instance_box box = make_owned_instance_box(new BaseT(*raw));
                return r_wrap_instance(std::move(box), cls, rv_policy::take_ownership, parent);
            } else {
                throw type_error("cannot copy non-copyable pointer target");
            }
        }
        instance_box box;
        box.type = std::type_index(typeid(BaseT));
        box.ptr = raw;
        box.owned = false;
        return r_wrap_instance(std::move(box), cls, policy, parent);
    }
};

template <typename T> inline SEXP to_r_sexp(T&& value, rv_policy policy, SEXP parent) {
    using DecayedT = remove_cvref_t<T>;
    if constexpr (std::is_same_v<DecayedT, none_t> || std::is_same_v<DecayedT, std::nullptr_t>) {
        return R_NilValue;
    } else if constexpr (std::is_array_v<std::remove_reference_t<T>> &&
                         std::is_same_v<std::remove_extent_t<std::remove_reference_t<T>>, char>) {
        return r_from_string(value);
    } else {
        return type_caster<DecayedT>::to_r(std::forward<T>(value), policy, parent);
    }
}

} // namespace pyrabind::r_detail

#define PYRABIND_MAKE_OPAQUE(...)                                                                  \
    namespace pyrabind::r_detail {                                                                 \
    template <> struct type_caster<__VA_ARGS__> : class_type_caster<__VA_ARGS__> {};               \
    }
