#include "internal.hpp"

namespace pyrabind::r_detail {

type_registry& type_registry::instance() {
    static type_registry reg;
    return reg;
}

RRuntimeState& RRuntimeState::instance() {
    static RRuntimeState st;
    return st;
}

void RRuntimeState::ensure_symbols() {
    if (initialized) {
        return;
    }
    instance_tag = Rf_install("pyrabind_instance");
    enum_tag = Rf_install("pyrabind_enum");
    fn_tag = Rf_install("pyrabind_function");
    cls_tag = Rf_install("pyrabind_class");
    meth_tag = Rf_install("pyrabind_method");
    prop_tag = Rf_install("pyrabind_property");
    capsule_tag = Rf_install("pyrabind_capsule");
    pb_attr_sym = Rf_install(".pb");
    stop_fn = Rf_findFun(Rf_install("stop"), R_BaseNamespace);
    keep_alive_pool = cpp11::sexp(R_NilValue);
    initialized = true;
}

namespace {

const char* condition_class_name(error_kind kind) noexcept {
    switch (kind) {
    case error_kind::value:
        return "pyrabind_value_error";
    case error_kind::type:
        return "pyrabind_type_error";
    case error_kind::index:
        return "pyrabind_index_error";
    case error_kind::key:
        return "pyrabind_key_error";
    case error_kind::attribute:
        return "pyrabind_attribute_error";
    case error_kind::stop_iteration:
        return "pyrabind_stop_iteration";
    case error_kind::runtime:
    default:
        return "pyrabind_runtime_error";
    }
}

} // namespace

[[noreturn]] void r_raise_condition(error_kind kind, const char* msg) {
    auto& rt = RRuntimeState::instance();
    rt.ensure_symbols();

    SEXP cond = PROTECT(Rf_allocVector(VECSXP, 2));
    SEXP msg_sexp = PROTECT(Rf_mkString(msg != nullptr ? msg : ""));
    SET_VECTOR_ELT(cond, 0, msg_sexp);
    SET_VECTOR_ELT(cond, 1, R_NilValue);

    SEXP nms = PROTECT(Rf_allocVector(STRSXP, 2));
    SET_STRING_ELT(nms, 0, Rf_mkChar("message"));
    SET_STRING_ELT(nms, 1, Rf_mkChar("call"));
    Rf_setAttrib(cond, R_NamesSymbol, nms);

    SEXP cls = PROTECT(Rf_allocVector(STRSXP, 4));
    SET_STRING_ELT(cls, 0, Rf_mkChar(condition_class_name(kind)));
    SET_STRING_ELT(cls, 1, Rf_mkChar("pyrabind_error"));
    SET_STRING_ELT(cls, 2, Rf_mkChar("error"));
    SET_STRING_ELT(cls, 3, Rf_mkChar("condition"));
    Rf_setAttrib(cond, R_ClassSymbol, cls);

    SEXP call = PROTECT(Rf_lang2(rt.stop_fn, cond));
    Rf_eval(call, R_BaseNamespace);
    UNPROTECT(5);
    Rf_error("%s", msg != nullptr ? msg : "pyrabind error");
}

const RInstanceHolder* r_unwrap_instance(SEXP x) {
    auto& rt = RRuntimeState::instance();
    rt.ensure_symbols();
    if (x == nullptr || x == R_NilValue || TYPEOF(x) != EXTPTRSXP ||
        R_ExternalPtrTag(x) != rt.instance_tag) {
        throw type_error("expected a pyrabind C++ object");
    }
    const auto* holder = static_cast<const RInstanceHolder*>(R_ExternalPtrAddr(x));
    if (holder == nullptr || holder->box.ptr == nullptr || holder->cls == nullptr) {
        throw type_error("invalid or finalized pyrabind C++ object");
    }
    return holder;
}

const REnumHolder* r_unwrap_enum(SEXP x) {
    auto& rt = RRuntimeState::instance();
    rt.ensure_symbols();
    if (x == nullptr || x == R_NilValue || TYPEOF(x) != EXTPTRSXP ||
        R_ExternalPtrTag(x) != rt.enum_tag) {
        throw type_error("expected a pyrabind enum instance");
    }
    const auto* holder = static_cast<const REnumHolder*>(R_ExternalPtrAddr(x));
    if (holder == nullptr || holder->rec == nullptr) {
        throw type_error("invalid or finalized pyrabind enum instance");
    }
    return holder;
}

void r_attach_keep_alive(SEXP nurse, SEXP patient) {
    if (nurse == nullptr || nurse == R_NilValue || patient == nullptr || patient == R_NilValue ||
        nurse == patient) {
        return;
    }
    if (TYPEOF(nurse) == EXTPTRSXP) {
        SEXP existing = R_ExternalPtrProtected(nurse);
        SEXP cell = PROTECT(Rf_cons(patient, existing));
        R_SetExternalPtrProtected(nurse, cell);
        UNPROTECT(1);
    } else {
        auto& rt = RRuntimeState::instance();
        rt.ensure_symbols();
        SEXP cell = PROTECT(Rf_cons(patient, rt.keep_alive_pool.data()));
        rt.keep_alive_pool = cpp11::sexp(cell);
        UNPROTECT(1);
    }
}

SEXP dispatch_overload_list(const std::vector<overload_record>& overloads, const call_args& cargs,
                            std::string_view fn_name, SEXP parent) {
    for (int pass = 0; pass < 2; ++pass) {
        const bool convert = (pass == 1);
        for (const auto& ov : overloads) {
            bool matched = false;
            SEXP res = ov.invoke(cargs, convert, matched, parent);
            if (matched) {
                return res;
            }
        }
    }
    throw type_error("incompatible arguments for '" + std::string(fn_name) + "'");
}

} // namespace pyrabind::r_detail
