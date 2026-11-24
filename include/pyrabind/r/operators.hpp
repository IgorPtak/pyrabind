#pragma once

#include "pyrabind/r/backend.hpp"

#include <cmath>
#include <cstdlib>
#include <functional>
#include <type_traits>

namespace pyrabind::r_detail {

struct self_t {};
struct undefined_t {};

enum op_id : int {
    op_add,
    op_sub,
    op_mul,
    op_truediv,
    op_mod,
    op_lshift,
    op_rshift,
    op_and,
    op_xor,
    op_or,
    op_eq,
    op_ne,
    op_lt,
    op_le,
    op_gt,
    op_ge,
    op_iadd,
    op_isub,
    op_imul,
    op_itruediv,
    op_imod,
    op_ilshift,
    op_irshift,
    op_iand,
    op_ixor,
    op_ior,
    op_neg,
    op_pos,
    op_invert,
    op_bool,
    op_abs,
    op_hash
};

enum op_side : int { op_l, op_r, op_u };

template <op_id Id, op_side Side, typename Base, typename L, typename R> struct op_impl;

template <op_id Id, op_side Side, typename L, typename R> struct op_expr {
    template <typename Class, typename... Extra> void execute(Class& cl, Extra&&... extra) const {
        using Type = typename Class::Type;
        using Lt = std::conditional_t<std::is_same_v<L, self_t>, Type, L>;
        using Rt = std::conditional_t<std::is_same_v<R, self_t>, Type, R>;
        using Impl = op_impl<Id, Side, Type, Lt, Rt>;
        cl.def(Impl::name(), &Impl::execute, is_operator(), Impl::default_policy,
               std::forward<Extra>(extra)...);
    }
};

#define PYRABIND_BINARY_OP(id, rid, sym, expr)                                                     \
    template <typename B, typename L, typename R> struct op_impl<op_##id, op_l, B, L, R> {         \
        static constexpr auto default_policy = rv_policy::automatic;                               \
        static constexpr const char* name() noexcept { return "__" #id "__"; }                     \
        static auto execute(const L& l, const R& r) -> decltype(expr) { return (expr); }           \
    };                                                                                             \
    template <typename B, typename L, typename R> struct op_impl<op_##id, op_r, B, L, R> {         \
        static constexpr auto default_policy = rv_policy::automatic;                               \
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

#define PYRABIND_INPLACE_OP(id, sym, expr)                                                         \
    template <typename B, typename L, typename R> struct op_impl<op_##id, op_l, B, L, R> {         \
        static constexpr auto default_policy = rv_policy::reference_internal;                      \
        static constexpr const char* name() noexcept { return "__" #id "__"; }                     \
        static auto execute(L& l, const R& r) -> decltype(expr) { return (expr); }                 \
    };                                                                                             \
    inline op_expr<op_##id, op_l, self_t, self_t> sym(const self_t&, const self_t&) noexcept {     \
        return {};                                                                                 \
    }                                                                                              \
    template <typename T>                                                                          \
    inline op_expr<op_##id, op_l, self_t, T> sym(const self_t&, const T&) noexcept {               \
        return {};                                                                                 \
    }

#define PYRABIND_UNARY_OP(id, sym, expr)                                                           \
    template <typename B, typename L> struct op_impl<op_##id, op_u, B, L, undefined_t> {           \
        static constexpr auto default_policy = rv_policy::automatic;                               \
        static constexpr const char* name() noexcept { return "__" #id "__"; }                     \
        static auto execute(const L& l) -> decltype(expr) { return (expr); }                       \
    };                                                                                             \
    inline op_expr<op_##id, op_u, self_t, undefined_t> sym(const self_t&) noexcept {               \
        return {};                                                                                 \
    }

PYRABIND_BINARY_OP(add, radd, operator+, l + r)
PYRABIND_BINARY_OP(sub, rsub, operator-, l - r)
PYRABIND_BINARY_OP(mul, rmul, operator*, l* r)
PYRABIND_BINARY_OP(truediv, rtruediv, operator/, l / r)
PYRABIND_BINARY_OP(mod, rmod, operator%, l % r)
PYRABIND_BINARY_OP(lshift, rlshift, operator<<, l << r)
PYRABIND_BINARY_OP(rshift, rrshift, operator>>, l >> r)
PYRABIND_BINARY_OP(and, rand, operator&, l& r)
PYRABIND_BINARY_OP(xor, rxor, operator^, l ^ r)
PYRABIND_BINARY_OP(or, ror, operator|, l | r)
PYRABIND_BINARY_OP(eq, eq, operator==, l == r)
PYRABIND_BINARY_OP(ne, ne, operator!=, l != r)
PYRABIND_BINARY_OP(lt, gt, operator<, l < r)
PYRABIND_BINARY_OP(le, ge, operator<=, l <= r)
PYRABIND_BINARY_OP(gt, lt, operator>, l > r)
PYRABIND_BINARY_OP(ge, le, operator>=, l >= r)

PYRABIND_INPLACE_OP(iadd, operator+=, l += r)
PYRABIND_INPLACE_OP(isub, operator-=, l -= r)
PYRABIND_INPLACE_OP(imul, operator*=, l *= r)
PYRABIND_INPLACE_OP(itruediv, operator/=, l /= r)
PYRABIND_INPLACE_OP(imod, operator%=, l %= r)
PYRABIND_INPLACE_OP(ilshift, operator<<=, l <<= r)
PYRABIND_INPLACE_OP(irshift, operator>>=, l >>= r)
PYRABIND_INPLACE_OP(iand, operator&=, l &= r)
PYRABIND_INPLACE_OP(ixor, operator^=, l ^= r)
PYRABIND_INPLACE_OP(ior, operator|=, l |= r)

PYRABIND_UNARY_OP(neg, operator-, -l)
PYRABIND_UNARY_OP(pos, operator+, +l)
PYRABIND_UNARY_OP(invert, operator~, ~l)
PYRABIND_UNARY_OP(bool, operator!, !!l)
PYRABIND_UNARY_OP(abs, abs, std::abs(l))
PYRABIND_UNARY_OP(hash, hash, std::hash<L>()(l))

#undef PYRABIND_BINARY_OP
#undef PYRABIND_INPLACE_OP
#undef PYRABIND_UNARY_OP

inline constexpr self_t self{};

} // namespace pyrabind::r_detail

namespace pyrabind {
using r_detail::abs;
using r_detail::hash;
using r_detail::self;
} // namespace pyrabind
