#pragma once

#include "pyrabind/r/caster.hpp"
#include "pyrabind/r/dispatcher.hpp"

#include <cpp11/protect.hpp>
#include <cpp11/sexp.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace pyrabind::r_detail {

SEXP r_wrap_cpp_function(std::shared_ptr<function_record> rec);
SEXP r_invoke_r_callable(SEXP fn_sexp, const std::vector<SEXP>& args);

template <typename Ret, typename... Args> struct type_caster<std::function<Ret(Args...)>> {
    std::function<Ret(Args...)> val;

    bool from_r(SEXP x, bool) {
        if (x == R_NilValue) {
            val = nullptr;
            return true;
        }
        if (TYPEOF(x) != CLOSXP && TYPEOF(x) != BUILTINSXP && TYPEOF(x) != SPECIALSXP) {
            return false;
        }
        auto kept = std::make_shared<cpp11::sexp>(x);
        val = [kept](Args... args) -> Ret {
            std::vector<SEXP> r_args;
            r_args.reserve(sizeof...(Args));
            (r_args.push_back(to_r_sexp(std::forward<Args>(args))), ...);
            SEXP res = PROTECT(r_invoke_r_callable(kept->data(), r_args));
            if constexpr (std::is_void_v<Ret>) {
                UNPROTECT(1);
                return;
            } else {
                type_caster<remove_cvref_t<Ret>> rc;
                if (!rc.from_r(res, true)) {
                    UNPROTECT(1);
                    throw type_error("failed converting R callback return value to C++ type");
                }
                UNPROTECT(1);
                return rc.get();
            }
        };
        return true;
    }

    std::function<Ret(Args...)>& get() noexcept { return val; }

    static SEXP to_r(const std::function<Ret(Args...)>& fn, rv_policy, SEXP) {
        if (!fn) {
            return R_NilValue;
        }
        auto rec = std::make_shared<function_record>();
        rec->name = "<cpp_function>";
        rec->overloads.push_back(make_function_overload(fn));
        return r_wrap_cpp_function(std::move(rec));
    }
};

} // namespace pyrabind::r_detail
