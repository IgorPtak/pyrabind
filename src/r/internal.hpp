#pragma once

#include <cpp11/R.hpp>
#include <cpp11/protect.hpp>
#include <cpp11/sexp.hpp>

#include "pyrabind/r/backend.hpp"
#include "pyrabind/r/registry.hpp"
#include "pyrabind/r/types.hpp"

#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace pyrabind::r_detail {

using custom_exception_translator =
    std::function<bool(std::exception_ptr, std::vector<std::string>&, std::string&)>;

struct RClassCache {
    cpp11::sexp cls_env;
    cpp11::sexp s3_classes;
};

struct REnumCache {
    cpp11::sexp s3_classes;
};

struct RRuntimeState {
    SEXP instance_tag = R_NilValue;
    SEXP enum_tag = R_NilValue;
    SEXP fn_tag = R_NilValue;
    SEXP cls_tag = R_NilValue;
    SEXP meth_tag = R_NilValue;
    SEXP prop_tag = R_NilValue;
    SEXP capsule_tag = R_NilValue;
    SEXP iter_tag = R_NilValue;
    SEXP pb_attr_sym = R_NilValue;
    SEXP stop_fn = R_NilValue;
    cpp11::sexp keep_alive_pool;
    cpp11::sexp dispatch_env;

    std::unordered_map<const class_record*, RClassCache> class_cache;
    std::unordered_map<const enum_record*, REnumCache> enum_cache;
    std::vector<custom_exception_translator> exception_translators;
    bool initialized = false;

    static RRuntimeState& instance();

    void ensure_symbols();
};

struct RInstanceHolder {
    instance_box box;
    const class_record* cls = nullptr;
};

struct REnumHolder {
    std::int64_t value = 0;
    const enum_record* rec = nullptr;
};

struct RFuncHolder {
    const function_record* rec = nullptr;
    std::shared_ptr<function_record> owned_rec;
};

[[noreturn]] void r_raise_condition(error_kind kind, const char* msg);
[[noreturn]] void r_raise_custom_condition(const std::vector<std::string>& classes,
                                           const char* msg);

template <typename Fn> SEXP r_error_boundary(Fn&& fn) noexcept {
    SEXP unwind_token = R_NilValue;
    error_kind err_kind = error_kind::runtime;
    bool has_error = false;
    bool has_custom_error = false;
    std::vector<std::string> custom_classes;
    std::string err_msg;

    try {
        return fn();
    } catch (const cpp11::unwind_exception& e) {
        unwind_token = e.token;
    } catch (const builtin_exception& e) {
        err_kind = e.kind();
        err_msg = e.what();
        has_error = true;
    } catch (...) {
        auto& rt = RRuntimeState::instance();
        std::exception_ptr eptr = std::current_exception();
        for (auto it = rt.exception_translators.rbegin(); it != rt.exception_translators.rend();
             ++it) {
            std::vector<std::string> cls_out;
            std::string msg_out;
            if ((*it)(eptr, cls_out, msg_out)) {
                custom_classes = std::move(cls_out);
                err_msg = std::move(msg_out);
                has_custom_error = true;
                break;
            }
        }
        if (!has_custom_error) {
            try {
                std::rethrow_exception(eptr);
            } catch (const std::invalid_argument& e) {
                err_kind = error_kind::value;
                err_msg = e.what();
            } catch (const std::out_of_range& e) {
                err_kind = error_kind::index;
                err_msg = e.what();
            } catch (const std::exception& e) {
                err_kind = error_kind::runtime;
                err_msg = e.what();
            } catch (...) {
                err_kind = error_kind::runtime;
                err_msg = "unknown C++ exception";
            }
            has_error = true;
        }
    }

    if (unwind_token != R_NilValue) {
        R_ContinueUnwind(unwind_token);
    }
    if (has_custom_error) {
        r_raise_custom_condition(custom_classes, err_msg.c_str());
    }
    if (has_error) {
        r_raise_condition(err_kind, err_msg.c_str());
    }
    return R_NilValue;
}

const RInstanceHolder* r_unwrap_instance(SEXP x);
const REnumHolder* r_unwrap_enum(SEXP x);

SEXP dispatch_overload_list(const std::vector<overload_record>& overloads, const call_args& cargs,
                            std::string_view fn_name, SEXP parent = R_NilValue);

} // namespace pyrabind::r_detail
