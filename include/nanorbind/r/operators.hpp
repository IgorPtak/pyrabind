#pragma once

#include "nanorbind/r/backend.hpp"

#include <type_traits>

namespace nanorbind::r_detail {

struct self_t {};
struct undefined_t {};

enum op_id : int {
    op_add,
    op_sub,
    op_mul,
    op_truediv,
    op_neg,
    op_eq,
    op_ne,
    op_lt,
    op_le,
    op_gt,
    op_ge
};

enum op_side : int { op_l, op_r, op_u };

template <op_id Id, op_side Side, typename Base, typename L, typename R> struct op_impl;

template <op_id Id, op_side Side, typename L, typename R> struct op_expr {
    template <typename Class, typename... Extra> void execute(Class& cl, Extra&&... extra) const {
        using Type = typename Class::Type;
        using Lt = std::conditional_t<std::is_same_v<L, self_t>, Type, L>;
        using Rt = std::conditional_t<std::is_same_v<R, self_t>, Type, R>;
        using Impl = op_impl<Id, Side, Type, Lt, Rt>;
        cl.def(Impl::name(), &Impl::execute, is_operator(), std::forward<Extra>(extra)...);
    }
};

#define NANORBIND_BINARY_OP(id, rid, sym, expr)                                                    \
    template <typename B, typename L, typename R> struct op_impl<op_##id, op_l, B, L, R> {         \
        static constexpr const char* name() noexcept { return "__" #id "__"; }                     \
        static auto execute(const L& l, const R& r) -> decltype(expr) { return (expr); }           \
    };                                                                                             \
    template <typename B, typename L, typename R> struct op_impl<op_##id, op_r, B, L, R> {         \
        static constexpr const char* name() noexcept { return "__" #rid "__"; }                    \
        static auto execute(const R& r, const L& l) -> decltype(expr) { return (expr); }           \
    };                                                                                             \
    inline op_expr<op_##id, op_l, self_t, self_t> sym(const self_t&, const self_t&) noexcept {     \
        return {};                                                                                 \
    }                                                                                              \
    template <typename T>                                                                          \
    inline op_expr<op_##id, op_l, self_t, T> sym(const self_t&, const T&) noexcept {               \
        return {};                                                                                 \
    }                                                                                              \
    template <typename T>                                                                          \
    inline op_expr<op_##id, op_r, T, self_t> sym(const T&, const self_t&) noexcept {               \
        return {};                                                                                 \
    }

#define NANORBIND_UNARY_OP(id, sym, expr)                                                          \
    template <typename B, typename L> struct op_impl<op_##id, op_u, B, L, undefined_t> {           \
        static constexpr const char* name() noexcept { return "__" #id "__"; }                     \
        static auto execute(const L& l) -> decltype(expr) { return (expr); }                       \
    };                                                                                             \
    inline op_expr<op_##id, op_u, self_t, undefined_t> sym(const self_t&) noexcept {               \
        return {};                                                                                 \
    }

NANORBIND_BINARY_OP(add, radd, operator+, l + r)
NANORBIND_BINARY_OP(sub, rsub, operator-, l - r)
NANORBIND_BINARY_OP(mul, rmul, operator*, l* r)
NANORBIND_BINARY_OP(truediv, rtruediv, operator/, l / r)
NANORBIND_BINARY_OP(eq, eq, operator==, l == r)
NANORBIND_BINARY_OP(ne, ne, operator!=, l != r)
NANORBIND_BINARY_OP(lt, gt, operator<, l < r)
NANORBIND_BINARY_OP(le, ge, operator<=, l <= r)
NANORBIND_BINARY_OP(gt, lt, operator>, l > r)
NANORBIND_BINARY_OP(ge, le, operator>=, l >= r)
NANORBIND_UNARY_OP(neg, operator-, -l)

#undef NANORBIND_BINARY_OP
#undef NANORBIND_UNARY_OP

inline constexpr self_t self{};

} // namespace nanorbind::r_detail

namespace nanorbind {
using r_detail::self;
} // namespace nanorbind
