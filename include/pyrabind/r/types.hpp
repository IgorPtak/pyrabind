#pragma once

#include <cpp11/R.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace pyrabind {

enum class error_kind : std::uint8_t {
    runtime = 0,
    value,
    type,
    index,
    key,
    attribute,
    stop_iteration
};

class builtin_exception : public std::runtime_error {
public:
    builtin_exception(error_kind kind, const char* what)
        : std::runtime_error(what != nullptr ? what : ""), kind_(kind) {}
    builtin_exception(error_kind kind, const std::string& what)
        : std::runtime_error(what), kind_(kind) {}

    error_kind kind() const noexcept { return kind_; }

private:
    error_kind kind_;
};

inline builtin_exception value_error(const char* what = "") {
    return builtin_exception(error_kind::value, what);
}
inline builtin_exception value_error(const std::string& what) {
    return builtin_exception(error_kind::value, what);
}

inline builtin_exception type_error(const char* what = "") {
    return builtin_exception(error_kind::type, what);
}
inline builtin_exception type_error(const std::string& what) {
    return builtin_exception(error_kind::type, what);
}

inline builtin_exception index_error(const char* what = "") {
    return builtin_exception(error_kind::index, what);
}
inline builtin_exception index_error(const std::string& what) {
    return builtin_exception(error_kind::index, what);
}

inline builtin_exception key_error(const char* what = "") {
    return builtin_exception(error_kind::key, what);
}
inline builtin_exception key_error(const std::string& what) {
    return builtin_exception(error_kind::key, what);
}

inline builtin_exception attribute_error(const char* what = "") {
    return builtin_exception(error_kind::attribute, what);
}
inline builtin_exception attribute_error(const std::string& what) {
    return builtin_exception(error_kind::attribute, what);
}

inline builtin_exception stop_iteration(const char* what = "") {
    return builtin_exception(error_kind::stop_iteration, what);
}

struct none_t {};
inline constexpr none_t none() noexcept {
    return {};
}

class capsule {
public:
    capsule() = default;

    capsule(const void* ptr, void (*cleanup)(void*) noexcept = nullptr)
        : capsule(ptr, nullptr, cleanup) {}

    capsule(const void* ptr, const char* cname, void (*cleanup)(void*) noexcept = nullptr)
        : ptr_(const_cast<void*>(ptr)), name_(cname) {
        if (ptr_ != nullptr && cleanup != nullptr) {
            void* raw = ptr_;
            owner_ = std::shared_ptr<void>(raw, [cleanup](void* p) noexcept { cleanup(p); });
        }
    }

    const char* name() const noexcept { return name_; }
    void* data() const noexcept { return ptr_; }
    const std::shared_ptr<void>& owner() const noexcept { return owner_; }
    bool is_valid() const noexcept { return ptr_ != nullptr; }

private:
    void* ptr_ = nullptr;
    const char* name_ = nullptr;
    std::shared_ptr<void> owner_;
};

class rv_policy {
public:
    enum value : std::uint8_t {
        automatic_v = 0,
        automatic_reference_v,
        take_ownership_v,
        copy_v,
        move_v,
        reference_v,
        reference_internal_v,
        none_v
    };

    template <value V> struct policy_tag {
        static constexpr value policy = V;
        constexpr operator rv_policy() const noexcept { return rv_policy(V); }
        constexpr operator value() const noexcept { return V; }
    };

    constexpr rv_policy(value v = automatic_v) noexcept : val_(v) {}
    template <value V> constexpr rv_policy(policy_tag<V>) noexcept : val_(V) {}
    constexpr operator value() const noexcept { return val_; }

    static constexpr policy_tag<automatic_v> automatic{};
    static constexpr policy_tag<automatic_reference_v> automatic_reference{};
    static constexpr policy_tag<take_ownership_v> take_ownership{};
    static constexpr policy_tag<copy_v> copy{};
    static constexpr policy_tag<move_v> move{};
    static constexpr policy_tag<reference_v> reference{};
    static constexpr policy_tag<reference_internal_v> reference_internal{};
    static constexpr policy_tag<none_v> none{};

private:
    value val_;
};

class handle {
public:
    constexpr handle() noexcept = default;
    constexpr explicit handle(SEXP p) noexcept : ptr_(p) {}

    SEXP ptr() const noexcept { return ptr_; }
    bool is_valid() const noexcept { return ptr_ != nullptr && ptr_ != R_NilValue; }
    explicit operator bool() const noexcept { return is_valid(); }

private:
    SEXP ptr_ = R_NilValue;
};

template <typename T> inline handle type() noexcept {
    return handle{};
}

struct gil_scoped_acquire {
    gil_scoped_acquire() noexcept = default;
    ~gil_scoped_acquire() noexcept {}
    gil_scoped_acquire(const gil_scoped_acquire&) = delete;
    gil_scoped_acquire& operator=(const gil_scoped_acquire&) = delete;
    gil_scoped_acquire(gil_scoped_acquire&&) = delete;
    gil_scoped_acquire& operator=(gil_scoped_acquire&&) = delete;

    bool is_valid() const noexcept { return true; }
    explicit operator bool() const noexcept { return true; }
};

struct gil_scoped_release {
    gil_scoped_release() noexcept = default;
    ~gil_scoped_release() noexcept {}
    gil_scoped_release(const gil_scoped_release&) = delete;
    gil_scoped_release& operator=(const gil_scoped_release&) = delete;
    gil_scoped_release(gil_scoped_release&&) = delete;
    gil_scoped_release& operator=(gil_scoped_release&&) = delete;
};

template <typename... Guards> struct call_guard {};

struct is_method {};
struct is_operator {};
struct is_implicit {};
struct is_arithmetic {};
struct kw_only {};
struct const_t {};
inline constexpr const_t const_{};

template <std::size_t Nurse, std::size_t Patient> struct keep_alive {};

template <typename... Args> struct init {};

template <typename Func> struct new_factory {
    Func fn;
    explicit new_factory(Func f) : fn(std::move(f)) {}
};

template <typename Func> inline new_factory<std::decay_t<Func>> new_(Func&& f) {
    return new_factory<std::decay_t<Func>>(std::forward<Func>(f));
}

template <typename... Args> struct overload_cast_impl {
    template <typename Return> constexpr auto operator()(Return (*pmf)(Args...)) const noexcept {
        return pmf;
    }

    template <typename Return, typename Class>
    constexpr auto operator()(Return (Class::*pmf)(Args...)) const noexcept {
        return pmf;
    }

    template <typename Return, typename Class>
    constexpr auto operator()(Return (Class::*pmf)(Args...) const, const_t) const noexcept {
        return pmf;
    }
};

template <typename... Args> inline constexpr overload_cast_impl<Args...> overload_cast{};

namespace r_detail {

template <typename T>
SEXP to_r_sexp(T&& value, rv_policy policy = rv_policy::automatic, SEXP parent = R_NilValue);

} // namespace r_detail

struct arg_v {
    const char* name = nullptr;
    bool convert = true;
    bool accepts_none = false;
    std::function<SEXP()> default_fn;
};

struct arg {
    const char* name = nullptr;
    bool convert = true;
    bool accepts_none = false;

    constexpr explicit arg(const char* arg_name = nullptr, bool conv = true,
                           bool accept_none = false) noexcept
        : name(arg_name), convert(conv), accepts_none(accept_none) {}

    constexpr arg noconvert() const noexcept { return arg(name, false, accepts_none); }
    constexpr arg none() const noexcept { return arg(name, convert, true); }

    arg_v operator=(none_t) const {
        return arg_v{name, convert, true, []() -> SEXP { return R_NilValue; }};
    }

    arg_v operator=(std::nullptr_t) const {
        return arg_v{name, convert, true, []() -> SEXP { return R_NilValue; }};
    }

    template <typename T> arg_v operator=(T&& default_val) const {
        using ValT = std::decay_t<T>;
        if constexpr (std::is_same_v<ValT, const char*> ||
                      std::is_array_v<std::remove_reference_t<T>>) {
            std::string stored(default_val);
            return arg_v{name, convert, accepts_none, [stored = std::move(stored)]() -> SEXP {
                             return r_detail::to_r_sexp(stored);
                         }};
        } else {
            return arg_v{name, convert, accepts_none,
                         [stored = ValT(std::forward<T>(default_val))]() -> SEXP {
                             return r_detail::to_r_sexp(stored);
                         }};
        }
    }
};

namespace literals {
constexpr arg operator""_a(const char* name, std::size_t) noexcept {
    return arg(name);
}
} // namespace literals

} // namespace pyrabind
