#include "internal.hpp"

#include "pyrabind/r/dataframe.hpp"
#include "pyrabind/r/ndarray.hpp"
#include "pyrabind/r/stl/function.hpp"
#include "pyrabind/r/stl/vector.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace pyrabind::r_detail {

namespace {

void r_instance_finalizer(SEXP xptr) noexcept {
    void* raw = R_ExternalPtrAddr(xptr);
    if (raw != nullptr) {
        delete static_cast<RInstanceHolder*>(raw);
        R_ClearExternalPtr(xptr);
    }
}

void r_enum_finalizer(SEXP xptr) noexcept {
    void* raw = R_ExternalPtrAddr(xptr);
    if (raw != nullptr) {
        delete static_cast<REnumHolder*>(raw);
        R_ClearExternalPtr(xptr);
    }
}

struct RCapsuleHolder {
    capsule cap;
};

void r_capsule_finalizer(SEXP xptr) noexcept {
    void* raw = R_ExternalPtrAddr(xptr);
    if (raw != nullptr) {
        delete static_cast<RCapsuleHolder*>(raw);
        R_ClearExternalPtr(xptr);
    }
}

void r_func_finalizer(SEXP xptr) noexcept {
    void* raw = R_ExternalPtrAddr(xptr);
    if (raw != nullptr) {
        delete static_cast<RFuncHolder*>(raw);
        R_ClearExternalPtr(xptr);
    }
}

bool is_finite_integral(double d) noexcept {
    return std::isfinite(d) && std::floor(d) == d;
}

bool is_pure_ascii_bytes(const char* p, int len) noexcept {
    for (int i = 0; i < len; ++i) {
        if (static_cast<unsigned char>(p[i]) >= 0x80U) {
            return false;
        }
    }
    return true;
}

std::string translate_r_string(SEXP ch) {
    const cetype_t ce = Rf_getCharCE(ch);
    const char* raw = CHAR(ch);
    const int len = Rf_length(ch);
    if (ce == CE_UTF8 || is_pure_ascii_bytes(raw, len)) {
        return std::string(raw, static_cast<std::size_t>(len));
    }
    return cpp11::unwind_protect([&]() { return std::string(Rf_translateCharUTF8(ch)); });
}

} // namespace

bool r_to_bool(SEXP x, bool convert, bool& out) {
    if (x == nullptr || x == R_NilValue) {
        return false;
    }
    const SEXPTYPE tp = static_cast<SEXPTYPE>(TYPEOF(x));
    if (tp == LGLSXP && Rf_xlength(x) == 1) {
        const int v = LOGICAL_RO(x)[0];
        if (v == NA_LOGICAL) {
            return false;
        }
        out = (v != 0);
        return true;
    }
    if (!convert) {
        return false;
    }
    if (tp == INTSXP && Rf_xlength(x) == 1) {
        const int v = INTEGER_RO(x)[0];
        if (v == NA_INTEGER) {
            return false;
        }
        out = (v != 0);
        return true;
    }
    if (tp == REALSXP && Rf_xlength(x) == 1) {
        const double v = REAL_RO(x)[0];
        if (!std::isfinite(v)) {
            return false;
        }
        out = (v != 0.0);
        return true;
    }
    return false;
}

bool r_to_i64(SEXP x, bool convert, std::int64_t& out) {
    if (x == nullptr || x == R_NilValue || Rf_xlength(x) != 1) {
        return false;
    }
    const SEXPTYPE tp = static_cast<SEXPTYPE>(TYPEOF(x));
    if (tp == INTSXP) {
        const int v = INTEGER_RO(x)[0];
        if (v == NA_INTEGER) {
            return false;
        }
        out = static_cast<std::int64_t>(v);
        return true;
    }
    if (tp == REALSXP) {
        const double v = REAL_RO(x)[0];
        // Allow doubles outside 32-bit int range in exact pass, otherwise require convert=true.
        if (!convert && v >= -2147483647.0 && v <= 2147483647.0) {
            return false;
        }
        if (!is_finite_integral(v)) {
            return false;
        }
        if (v < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
            v >= 9223372036854775808.0) {
            return false;
        }
        out = static_cast<std::int64_t>(v);
        return true;
    }
    if (convert && tp == LGLSXP) {
        const int v = LOGICAL_RO(x)[0];
        if (v == NA_LOGICAL) {
            return false;
        }
        out = (v != 0) ? 1 : 0;
        return true;
    }
    return false;
}

bool r_to_u64(SEXP x, bool convert, std::uint64_t& out) {
    std::int64_t s = 0;
    if (r_to_i64(x, convert, s)) {
        if (s < 0) {
            return false;
        }
        out = static_cast<std::uint64_t>(s);
        return true;
    }
    if (x != nullptr && x != R_NilValue && TYPEOF(x) == REALSXP && Rf_xlength(x) == 1) {
        const double v = REAL_RO(x)[0];
        if (is_finite_integral(v) && v >= 0.0 && v < 18446744073709551616.0) {
            out = static_cast<std::uint64_t>(v);
            return true;
        }
    }
    return false;
}

bool r_to_double(SEXP x, bool convert, double& out) {
    if (x == nullptr || x == R_NilValue || Rf_xlength(x) != 1) {
        return false;
    }
    const SEXPTYPE tp = static_cast<SEXPTYPE>(TYPEOF(x));
    if (tp == REALSXP) {
        out = REAL_RO(x)[0];
        return true;
    }
    if (!convert) {
        return false;
    }
    if (tp == INTSXP) {
        const int v = INTEGER_RO(x)[0];
        if (v == NA_INTEGER) {
            return false;
        }
        out = static_cast<double>(v);
        return true;
    }
    if (tp == LGLSXP) {
        const int v = LOGICAL_RO(x)[0];
        if (v == NA_LOGICAL) {
            return false;
        }
        out = (v != 0) ? 1.0 : 0.0;
        return true;
    }
    return false;
}

bool r_to_complex(SEXP x, bool convert, double& re, double& im) {
    if (x == nullptr || x == R_NilValue || Rf_xlength(x) != 1) {
        return false;
    }
    const SEXPTYPE tp = static_cast<SEXPTYPE>(TYPEOF(x));
    if (tp == CPLXSXP) {
        const Rcomplex c = COMPLEX_RO(x)[0];
        re = c.r;
        im = c.i;
        return true;
    }
    if (!convert) {
        return false;
    }
    if (tp == REALSXP) {
        re = REAL_RO(x)[0];
        im = 0.0;
        return true;
    }
    if (tp == INTSXP) {
        const int v = INTEGER_RO(x)[0];
        if (v == NA_INTEGER) {
            return false;
        }
        re = static_cast<double>(v);
        im = 0.0;
        return true;
    }
    return false;
}

bool r_to_duration_secs(SEXP x, bool convert, double& out_secs) {
    if (x == nullptr || x == R_NilValue || Rf_xlength(x) != 1) {
        return false;
    }
    const SEXPTYPE tp = static_cast<SEXPTYPE>(TYPEOF(x));
    double raw_val = 0.0;
    if (tp == REALSXP) {
        raw_val = REAL_RO(x)[0];
    } else if (tp == INTSXP) {
        const int v = INTEGER_RO(x)[0];
        if (v == NA_INTEGER) {
            return false;
        }
        raw_val = static_cast<double>(v);
    } else {
        return false;
    }

    if (Rf_inherits(x, "difftime")) {
        SEXP units_attr = Rf_getAttrib(x, Rf_install("units"));
        double scale = 1.0;
        if (units_attr != R_NilValue && TYPEOF(units_attr) == STRSXP &&
            Rf_xlength(units_attr) == 1) {
            const char* u = CHAR(STRING_ELT(units_attr, 0));
            if (std::strcmp(u, "mins") == 0) {
                scale = 60.0;
            } else if (std::strcmp(u, "hours") == 0) {
                scale = 3600.0;
            } else if (std::strcmp(u, "days") == 0) {
                scale = 86400.0;
            } else if (std::strcmp(u, "weeks") == 0) {
                scale = 604800.0;
            }
        }
        out_secs = raw_val * scale;
        return true;
    }

    if (!convert) {
        return false;
    }
    out_secs = raw_val;
    return true;
}

bool r_to_posix_secs(SEXP x, double& out_epoch_secs) {
    if (x == nullptr || x == R_NilValue || Rf_xlength(x) != 1) {
        return false;
    }
    const SEXPTYPE tp = static_cast<SEXPTYPE>(TYPEOF(x));
    double raw_val = 0.0;
    if (tp == REALSXP) {
        raw_val = REAL_RO(x)[0];
    } else if (tp == INTSXP) {
        const int v = INTEGER_RO(x)[0];
        if (v == NA_INTEGER) {
            return false;
        }
        raw_val = static_cast<double>(v);
    } else {
        return false;
    }
    if (Rf_inherits(x, "POSIXct")) {
        out_epoch_secs = raw_val;
        return true;
    }
    if (Rf_inherits(x, "Date")) {
        out_epoch_secs = raw_val * 86400.0;
        return true;
    }
    return false;
}

bool r_to_string(SEXP x, bool, std::string& out) {
    if (x == nullptr || x == R_NilValue || TYPEOF(x) != STRSXP || Rf_xlength(x) != 1) {
        return false;
    }
    SEXP elt = STRING_ELT(x, 0);
    if (elt == NA_STRING) {
        return false;
    }
    out = translate_r_string(elt);
    return true;
}

bool r_to_cstr(SEXP x, bool, const char*& out) {
    if (x == nullptr || x == R_NilValue) {
        out = nullptr;
        return true;
    }
    if (TYPEOF(x) != STRSXP || Rf_xlength(x) != 1) {
        return false;
    }
    SEXP elt = STRING_ELT(x, 0);
    if (elt == NA_STRING) {
        return false;
    }
    out = cpp11::unwind_protect([&]() { return Rf_translateCharUTF8(elt); });
    return true;
}

SEXP r_from_bool(bool v) {
    return Rf_ScalarLogical(v ? 1 : 0);
}

SEXP r_from_i64(std::int64_t v) {
    if (v > static_cast<std::int64_t>(NA_INTEGER) && v <= 2147483647LL) {
        return Rf_ScalarInteger(static_cast<int>(v));
    }
    return Rf_ScalarReal(static_cast<double>(v));
}

SEXP r_from_u64(std::uint64_t v) {
    if (v <= 2147483647ULL) {
        return Rf_ScalarInteger(static_cast<int>(v));
    }
    return Rf_ScalarReal(static_cast<double>(v));
}

SEXP r_from_double(double v) {
    return Rf_ScalarReal(v);
}

SEXP r_from_complex(double re, double im) {
    Rcomplex c{};
    c.r = re;
    c.i = im;
    return Rf_ScalarComplex(c);
}

SEXP r_from_duration_secs(double secs) {
    SEXP out = PROTECT(cpp11::safe[Rf_allocVector](REALSXP, 1));
    REAL(out)[0] = secs;
    SEXP cls = PROTECT(Rf_mkString("difftime"));
    SEXP units = PROTECT(Rf_mkString("secs"));
    Rf_setAttrib(out, R_ClassSymbol, cls);
    Rf_setAttrib(out, Rf_install("units"), units);
    UNPROTECT(3);
    return out;
}

SEXP r_from_posix_secs(double epoch_secs) {
    SEXP out = PROTECT(cpp11::safe[Rf_allocVector](REALSXP, 1));
    REAL(out)[0] = epoch_secs;
    SEXP cls = PROTECT(cpp11::safe[Rf_allocVector](STRSXP, 2));
    SET_STRING_ELT(cls, 0, Rf_mkChar("POSIXct"));
    SET_STRING_ELT(cls, 1, Rf_mkChar("POSIXt"));
    SEXP tz = PROTECT(Rf_mkString("UTC"));
    Rf_setAttrib(out, R_ClassSymbol, cls);
    Rf_setAttrib(out, Rf_install("tzone"), tz);
    UNPROTECT(3);
    return out;
}

SEXP r_from_string(std::string_view v) {
    SEXP ch = PROTECT(cpp11::unwind_protect(
        [&]() { return Rf_mkCharLenCE(v.data(), static_cast<int>(v.size()), CE_UTF8); }));
    SEXP out = Rf_ScalarString(ch);
    UNPROTECT(1);
    return out;
}

bool r_extract_instance(SEXP x, std::type_index target_type, void*& out_ptr) {
    auto& rt = RRuntimeState::instance();
    rt.ensure_symbols();
    if (x == nullptr || x == R_NilValue || TYPEOF(x) != EXTPTRSXP ||
        R_ExternalPtrTag(x) != rt.instance_tag) {
        return false;
    }
    const auto* holder = static_cast<const RInstanceHolder*>(R_ExternalPtrAddr(x));
    if (holder == nullptr || holder->box.ptr == nullptr) {
        return false;
    }
    void* casted = holder->box.cast_to(target_type);
    if (casted == nullptr) {
        return false;
    }
    out_ptr = casted;
    return true;
}

bool r_extract_shared_holder(SEXP x, std::type_index target_type, void*& out_ptr,
                             std::shared_ptr<void>& out_owner) {
    auto& rt = RRuntimeState::instance();
    rt.ensure_symbols();
    if (x == nullptr || x == R_NilValue || TYPEOF(x) != EXTPTRSXP ||
        R_ExternalPtrTag(x) != rt.instance_tag) {
        return false;
    }
    const auto* holder = static_cast<const RInstanceHolder*>(R_ExternalPtrAddr(x));
    if (holder == nullptr || holder->box.ptr == nullptr) {
        return false;
    }
    void* casted = holder->box.cast_to(target_type);
    if (casted == nullptr) {
        return false;
    }
    out_ptr = casted;
    if (holder->box.holder) {
        out_owner = holder->box.holder;
    } else {
        out_owner = std::make_shared<cpp11::sexp>(x);
    }
    return true;
}

bool r_relinquish_unique_ptr(SEXP x, std::type_index target_type, bool poly_or_vd, void*& out_ptr) {
    auto& rt = RRuntimeState::instance();
    rt.ensure_symbols();
    if (x == nullptr || x == R_NilValue || TYPEOF(x) != EXTPTRSXP ||
        R_ExternalPtrTag(x) != rt.instance_tag) {
        return false;
    }
    auto* holder = static_cast<RInstanceHolder*>(R_ExternalPtrAddr(x));
    if (holder == nullptr || holder->box.ptr == nullptr || !holder->box.owned ||
        holder->box.ctrl == nullptr || holder->box.ctrl->released ||
        holder->box.holder.use_count() != 1) {
        return false;
    }
    if (!poly_or_vd && holder->box.type != target_type &&
        holder->box.ctrl->alloc_type != target_type) {
        return false;
    }
    void* casted = holder->box.cast_to(target_type);
    if (casted == nullptr) {
        return false;
    }
    if (!poly_or_vd && casted != holder->box.ctrl->raw) {
        return false;
    }
    holder->box.ctrl->released = true;
    holder->box.ptr = nullptr;
    holder->box.owned = false;
    holder->box.holder.reset();
    holder->box.ctrl = nullptr;
    out_ptr = casted;
    return true;
}

SEXP r_wrap_instance(instance_box box, const class_record* cls, rv_policy policy, SEXP parent) {
    auto& rt = RRuntimeState::instance();
    rt.ensure_symbols();

    SEXP prot = R_NilValue;
    if ((policy == rv_policy::reference_internal || policy == rv_policy::automatic_reference) &&
        parent != R_NilValue) {
        prot = parent;
    }

    auto* holder = new RInstanceHolder{std::move(box), cls};
    SEXP xptr = PROTECT(R_MakeExternalPtr(holder, rt.instance_tag, prot));
    R_RegisterCFinalizerEx(xptr, &r_instance_finalizer, TRUE);

    auto cache_it = rt.class_cache.find(cls);
    if (cache_it != rt.class_cache.end()) {
        Rf_setAttrib(xptr, rt.pb_attr_sym, cache_it->second.cls_env.data());
        Rf_setAttrib(xptr, R_ClassSymbol, cache_it->second.s3_classes.data());
    } else {
        SEXP s3 = PROTECT(Rf_mkString("pyrabind_object"));
        Rf_setAttrib(xptr, R_ClassSymbol, s3);
        UNPROTECT(1);
    }

    UNPROTECT(1);
    return xptr;
}

bool r_extract_enum(SEXP x, std::type_index enum_type, std::int64_t& out_val) {
    auto& rt = RRuntimeState::instance();
    rt.ensure_symbols();
    if (x == nullptr || x == R_NilValue || TYPEOF(x) != EXTPTRSXP ||
        R_ExternalPtrTag(x) != rt.enum_tag) {
        return false;
    }
    const auto* holder = static_cast<const REnumHolder*>(R_ExternalPtrAddr(x));
    if (holder == nullptr || holder->rec == nullptr || holder->rec->type != enum_type) {
        return false;
    }
    out_val = holder->value;
    return true;
}

SEXP r_wrap_enum(const enum_record* rec, std::int64_t val) {
    auto& rt = RRuntimeState::instance();
    rt.ensure_symbols();

    auto* holder = new REnumHolder{val, rec};
    SEXP xptr = PROTECT(R_MakeExternalPtr(holder, rt.enum_tag, R_NilValue));
    R_RegisterCFinalizerEx(xptr, &r_enum_finalizer, TRUE);

    auto it = rt.enum_cache.find(rec);
    if (it != rt.enum_cache.end()) {
        Rf_setAttrib(xptr, R_ClassSymbol, it->second.s3_classes.data());
    } else {
        SEXP s3 = PROTECT(Rf_mkString("pyrabind_enum"));
        Rf_setAttrib(xptr, R_ClassSymbol, s3);
        UNPROTECT(1);
    }
    if (rt.dispatch_env.data() != R_NilValue) {
        Rf_setAttrib(xptr, rt.pb_attr_sym, rt.dispatch_env.data());
    }

    UNPROTECT(1);
    return xptr;
}

SEXP r_wrap_capsule(const capsule& cap) {
    auto& rt = RRuntimeState::instance();
    rt.ensure_symbols();
    auto* holder = new RCapsuleHolder{cap};
    SEXP xptr = PROTECT(R_MakeExternalPtr(holder, rt.capsule_tag, R_NilValue));
    R_RegisterCFinalizerEx(xptr, &r_capsule_finalizer, TRUE);
    UNPROTECT(1);
    return xptr;
}

bool r_extract_capsule(SEXP x, capsule& out) {
    auto& rt = RRuntimeState::instance();
    rt.ensure_symbols();
    if (x == nullptr || x == R_NilValue || TYPEOF(x) != EXTPTRSXP ||
        R_ExternalPtrTag(x) != rt.capsule_tag) {
        return false;
    }
    const auto* holder = static_cast<const RCapsuleHolder*>(R_ExternalPtrAddr(x));
    if (holder == nullptr) {
        return false;
    }
    out = holder->cap;
    return true;
}

SEXP r_wrap_cpp_function(std::shared_ptr<function_record> rec) {
    auto& rt = RRuntimeState::instance();
    rt.ensure_symbols();

    auto* holder = new RFuncHolder{rec.get(), std::move(rec)};
    SEXP fn_xptr = PROTECT(R_MakeExternalPtr(holder, rt.fn_tag, rt.fn_tag));
    R_RegisterCFinalizerEx(fn_xptr, &r_func_finalizer, TRUE);

    SEXP disp_env = rt.dispatch_env.data();
    if (disp_env == R_NilValue) {
        UNPROTECT(1);
        throw type_error("pyrabind R dispatch environment not initialized");
    }
    SEXP make_fn = R_getVar(Rf_install("make_fn"), disp_env, FALSE);
    SEXP name_s = PROTECT(Rf_mkString("<cpp_function>"));
    SEXP doc_s = PROTECT(Rf_mkString(""));
    SEXP call = PROTECT(Rf_lang4(make_fn, fn_xptr, name_s, doc_s));
    SEXP out = cpp11::unwind_protect([&]() { return Rf_eval(call, disp_env); });
    UNPROTECT(4);
    return out;
}

SEXP r_invoke_r_callable(SEXP fn_sexp, const std::vector<SEXP>& args) {
    const R_xlen_t total = static_cast<R_xlen_t>(args.size()) + 1;
    SEXP call = PROTECT(cpp11::safe[Rf_allocVector](LANGSXP, total));
    SETCAR(call, fn_sexp);
    SEXP cur = CDR(call);
    for (SEXP arg_sexp : args) {
        SETCAR(cur, arg_sexp);
        cur = CDR(cur);
    }
    SEXP res = cpp11::unwind_protect([&]() { return Rf_eval(call, R_GlobalEnv); });
    UNPROTECT(1);
    return res;
}

bool r_acquire_ndarray(SEXP x, pb_dtype req_dtype, bool require_writable, std::int32_t req_ndim,
                       const std::int64_t* req_shape, char req_order, r_ndarray_desc& out) {
    if (x == nullptr || x == R_NilValue) {
        return false;
    }

    const SEXPTYPE tp = static_cast<SEXPTYPE>(TYPEOF(x));
    pb_dtype actual_dtype = pb_dtype::any;
    void* raw_ptr = nullptr;

    if (tp == REALSXP) {
        actual_dtype = pb_dtype::f64;
        if (require_writable) {
            if (ALTREP(x)) {
                return false;
            }
            raw_ptr = REAL(x);
        } else {
            raw_ptr = const_cast<double*>(REAL_RO(x));
        }
    } else if (tp == INTSXP) {
        actual_dtype = pb_dtype::i32;
        if (require_writable) {
            if (ALTREP(x)) {
                return false;
            }
            raw_ptr = INTEGER(x);
        } else {
            raw_ptr = const_cast<int*>(INTEGER_RO(x));
        }
    } else if (tp == RAWSXP) {
        actual_dtype = pb_dtype::u8;
        if (require_writable) {
            if (ALTREP(x)) {
                return false;
            }
            raw_ptr = RAW(x);
        } else {
            raw_ptr = const_cast<Rbyte*>(RAW_RO(x));
        }
    } else {
        return false;
    }

    if (req_dtype != pb_dtype::any && req_dtype != actual_dtype) {
        return false;
    }

    SEXP dims = Rf_getAttrib(x, R_DimSymbol);
    if (dims == R_NilValue || TYPEOF(dims) != INTSXP) {
        out.shape = {static_cast<std::int64_t>(Rf_xlength(x))};
        out.strides = {1};
    } else {
        const R_xlen_t nd = Rf_xlength(dims);
        const int* dp = INTEGER_RO(dims);
        out.shape.resize(static_cast<std::size_t>(nd));
        out.strides.resize(static_cast<std::size_t>(nd));
        std::int64_t stride = 1;
        for (R_xlen_t i = 0; i < nd; ++i) {
            out.shape[static_cast<std::size_t>(i)] = static_cast<std::int64_t>(dp[i]);
            out.strides[static_cast<std::size_t>(i)] = stride;
            stride *= static_cast<std::int64_t>(dp[i]);
        }
    }

    if (req_ndim >= 0 && static_cast<std::int32_t>(out.shape.size()) != req_ndim) {
        return false;
    }

    if (req_shape != nullptr) {
        for (std::size_t i = 0; i < out.shape.size(); ++i) {
            if (req_shape[i] >= 0 && out.shape[i] != req_shape[i]) {
                return false;
            }
        }
    }

    if (req_order == 'C' && out.shape.size() >= 2) {
        bool trivial_f = true;
        std::int64_t non_one = 0;
        for (std::int64_t d : out.shape) {
            if (d > 1) {
                ++non_one;
            }
            if (non_one > 1) {
                trivial_f = false;
                break;
            }
        }
        if (!trivial_f) {
            return false;
        }
    }

    out.data = raw_ptr;
    out.dtype = actual_dtype;
    if (x != R_NilValue) {
        auto holder = std::make_shared<cpp11::sexp>(x);
        out.lease = std::move(holder);
    }
    return true;
}

namespace {

template <typename SrcT, typename DstT>
void copy_ndarray_strided(const SrcT* src, DstT* dst, const r_ndarray_export& info) {
    if (info.ndim == 0) {
        return;
    }
    if (info.ndim == 1) {
        const std::int64_t n = info.shape[0];
        const std::int64_t s0 = info.strides[0];
        if constexpr (std::is_same_v<SrcT, DstT>) {
            if (s0 == 1 && n > 0) {
                std::memcpy(dst, src, static_cast<std::size_t>(n) * sizeof(DstT));
                return;
            }
        }
        for (std::int64_t i = 0; i < n; ++i) {
            dst[i] = static_cast<DstT>(src[i * s0]);
        }
        return;
    }
    if (info.ndim == 2) {
        const std::int64_t rows = info.shape[0];
        const std::int64_t cols = info.shape[1];
        const std::int64_t rs = info.strides[0];
        const std::int64_t cs = info.strides[1];
        if constexpr (std::is_same_v<SrcT, DstT>) {
            if (rs == 1 && cs == rows && rows > 0 && cols > 0) {
                std::memcpy(dst, src, static_cast<std::size_t>(rows * cols) * sizeof(DstT));
                return;
            }
        }
        for (std::int64_t c = 0; c < cols; ++c) {
            const std::int64_t col_off = c * cs;
            DstT* dst_col = dst + c * rows;
            for (std::int64_t r = 0; r < rows; ++r) {
                dst_col[r] = static_cast<DstT>(src[r * rs + col_off]);
            }
        }
        return;
    }

    std::size_t total = 1;
    for (std::size_t d = 0; d < info.ndim; ++d) {
        total *= static_cast<std::size_t>(info.shape[d]);
    }
    std::vector<std::int64_t> idx(info.ndim, 0);
    for (std::size_t flat = 0; flat < total; ++flat) {
        std::int64_t src_off = 0;
        for (std::size_t d = 0; d < info.ndim; ++d) {
            src_off += idx[d] * info.strides[d];
        }
        dst[flat] = static_cast<DstT>(src[src_off]);
        for (std::size_t d = 0; d < info.ndim; ++d) {
            if (++idx[d] < info.shape[d]) {
                break;
            }
            idx[d] = 0;
        }
    }
}

} // namespace

SEXP r_export_ndarray(const r_ndarray_export& info) {
    if (info.ndim == 0) {
        return R_NilValue;
    }
    std::size_t total = 1;
    for (std::size_t d = 0; d < info.ndim; ++d) {
        total *= static_cast<std::size_t>(info.shape[d]);
    }

    SEXPTYPE sexp_tp = REALSXP;
    switch (info.dtype) {
    case pb_dtype::f64:
    case pb_dtype::f32:
    case pb_dtype::i64:
        sexp_tp = REALSXP;
        break;
    case pb_dtype::i32:
        sexp_tp = INTSXP;
        break;
    case pb_dtype::bool8:
        sexp_tp = LGLSXP;
        break;
    case pb_dtype::u8:
        sexp_tp = RAWSXP;
        break;
    default:
        throw type_error("unsupported scalar dtype in R ndarray conversion");
    }

    SEXP out = R_NilValue;
    if (info.ndim == 2) {
        out = PROTECT(cpp11::safe[Rf_allocMatrix](sexp_tp, static_cast<int>(info.shape[0]),
                                                  static_cast<int>(info.shape[1])));
    } else {
        out = PROTECT(cpp11::safe[Rf_allocVector](sexp_tp, static_cast<R_xlen_t>(total)));
        if (info.ndim > 2) {
            SEXP dims =
                PROTECT(cpp11::safe[Rf_allocVector](INTSXP, static_cast<R_xlen_t>(info.ndim)));
            int* dp = INTEGER(dims);
            for (std::size_t d = 0; d < info.ndim; ++d) {
                dp[d] = static_cast<int>(info.shape[d]);
            }
            Rf_setAttrib(out, R_DimSymbol, dims);
            UNPROTECT(1);
        }
    }

    if (total > 0 && info.data != nullptr) {
        switch (info.dtype) {
        case pb_dtype::f64:
            copy_ndarray_strided(static_cast<const double*>(info.data), REAL(out), info);
            break;
        case pb_dtype::f32:
            copy_ndarray_strided(static_cast<const float*>(info.data), REAL(out), info);
            break;
        case pb_dtype::i64:
            copy_ndarray_strided(static_cast<const std::int64_t*>(info.data), REAL(out), info);
            break;
        case pb_dtype::i32:
            copy_ndarray_strided(static_cast<const std::int32_t*>(info.data), INTEGER(out), info);
            break;
        case pb_dtype::bool8:
            copy_ndarray_strided(static_cast<const bool*>(info.data), LOGICAL(out), info);
            break;
        case pb_dtype::u8:
            copy_ndarray_strided(static_cast<const std::uint8_t*>(info.data), RAW(out), info);
            break;
        default:
            break;
        }
    }

    UNPROTECT(1);
    return out;
}

bool r_to_data_frame(SEXP x, bool convert, data_frame& out) {
    out.names.clear();
    out.columns.clear();
    out.validity.clear();
    if (x == nullptr || x == R_NilValue || TYPEOF(x) != VECSXP) {
        return false;
    }
    if (!convert && !Rf_inherits(x, "data.frame")) {
        return false;
    }

    const R_xlen_t ncols = Rf_xlength(x);
    if (ncols == 0) {
        return true;
    }
    SEXP nms = Rf_getAttrib(x, R_NamesSymbol);
    if (nms == R_NilValue || TYPEOF(nms) != STRSXP || Rf_xlength(nms) != ncols) {
        return false;
    }

    std::size_t expected_rows = 0;
    bool have_rows = false;
    for (R_xlen_t c = 0; c < ncols; ++c) {
        SEXP nm_elt = STRING_ELT(nms, c);
        if (nm_elt == NA_STRING) {
            return false;
        }
        std::string col_name = translate_r_string(nm_elt);
        SEXP col_sexp = VECTOR_ELT(x, c);
        if (col_sexp == nullptr || col_sexp == R_NilValue) {
            return false;
        }

        const R_xlen_t n = Rf_xlength(col_sexp);
        if (!have_rows) {
            expected_rows = static_cast<std::size_t>(n);
            have_rows = true;
        } else if (static_cast<std::size_t>(n) != expected_rows) {
            return false;
        }

        const SEXPTYPE tp = static_cast<SEXPTYPE>(TYPEOF(col_sexp));
        column_data col;
        std::vector<std::uint8_t> mask;
        if (tp == REALSXP) {
            std::vector<double> vec(static_cast<std::size_t>(n));
            if (n > 0) {
                const double* rp = REAL_RO(col_sexp);
                std::memcpy(vec.data(), rp, static_cast<std::size_t>(n) * sizeof(double));
                for (R_xlen_t i = 0; i < n; ++i) {
                    if (ISNAN(rp[i])) {
                        if (mask.empty()) {
                            mask.assign(static_cast<std::size_t>(n), 1);
                        }
                        mask[static_cast<std::size_t>(i)] = 0;
                    }
                }
            }
            col = std::move(vec);
        } else if (tp == INTSXP) {
            std::vector<std::int32_t> vec(static_cast<std::size_t>(n), 0);
            if (n > 0) {
                const int* ip = INTEGER_RO(col_sexp);
                std::memcpy(vec.data(), ip, static_cast<std::size_t>(n) * sizeof(std::int32_t));
                for (R_xlen_t i = 0; i < n; ++i) {
                    if (ip[i] == NA_INTEGER) {
                        if (mask.empty()) {
                            mask.assign(static_cast<std::size_t>(n), 1);
                        }
                        mask[static_cast<std::size_t>(i)] = 0;
                        vec[static_cast<std::size_t>(i)] = 0;
                    }
                }
            }
            col = std::move(vec);
        } else if (tp == LGLSXP) {
            std::vector<bool> vec(static_cast<std::size_t>(n), false);
            const int* lp = LOGICAL_RO(col_sexp);
            for (R_xlen_t i = 0; i < n; ++i) {
                if (lp[i] == NA_LOGICAL) {
                    if (mask.empty()) {
                        mask.assign(static_cast<std::size_t>(n), 1);
                    }
                    mask[static_cast<std::size_t>(i)] = 0;
                } else {
                    vec[static_cast<std::size_t>(i)] = (lp[i] != 0);
                }
            }
            col = std::move(vec);
        } else if (tp == STRSXP) {
            std::vector<std::string> vec(static_cast<std::size_t>(n));
            for (R_xlen_t i = 0; i < n; ++i) {
                SEXP elt = STRING_ELT(col_sexp, i);
                if (elt == NA_STRING) {
                    if (mask.empty()) {
                        mask.assign(static_cast<std::size_t>(n), 1);
                    }
                    mask[static_cast<std::size_t>(i)] = 0;
                } else {
                    vec[static_cast<std::size_t>(i)] = translate_r_string(elt);
                }
            }
            col = std::move(vec);
        } else {
            return false;
        }

        out.add_column(std::move(col_name), std::move(col), std::move(mask));
    }
    return true;
}

SEXP r_from_data_frame(const data_frame& df) {
    if (df.names.size() != df.columns.size()) {
        throw value_error("data_frame names and columns size mismatch");
    }

    const R_xlen_t ncols = static_cast<R_xlen_t>(df.columns.size());
    const R_xlen_t nrows = static_cast<R_xlen_t>(df.nrows());

    SEXP out = PROTECT(cpp11::safe[Rf_allocVector](VECSXP, ncols));
    SEXP col_names = PROTECT(cpp11::safe[Rf_allocVector](STRSXP, ncols));

    for (R_xlen_t c = 0; c < ncols; ++c) {
        const std::size_t ci = static_cast<std::size_t>(c);
        const auto& cname = df.names[ci];
        SEXP nm_ch = PROTECT(cpp11::unwind_protect([&]() {
            return Rf_mkCharLenCE(cname.data(), static_cast<int>(cname.size()), CE_UTF8);
        }));
        SET_STRING_ELT(col_names, c, nm_ch);
        UNPROTECT(1);

        const bool has_null = df.has_nulls(ci);

        SEXP col_sexp = std::visit(
            [&](const auto& vec) -> SEXP {
                using VecT = std::decay_t<decltype(vec)>;
                if constexpr (std::is_same_v<VecT, std::vector<double>>) {
                    SEXP res = PROTECT(
                        type_caster<std::vector<double>>::to_r(vec, rv_policy::copy, R_NilValue));
                    if (has_null) {
                        double* rp = REAL(res);
                        for (R_xlen_t i = 0; i < nrows; ++i) {
                            if (df.is_null(ci, static_cast<std::size_t>(i))) {
                                rp[i] = NA_REAL;
                            }
                        }
                    }
                    UNPROTECT(1);
                    return res;
                } else if constexpr (std::is_same_v<VecT, std::vector<std::int64_t>>) {
                    if (!has_null) {
                        return type_caster<std::vector<std::int64_t>>::to_r(vec, rv_policy::copy,
                                                                            R_NilValue);
                    }
                    bool fits_int32 = true;
                    for (R_xlen_t i = 0; i < nrows; ++i) {
                        if (!df.is_null(ci, static_cast<std::size_t>(i))) {
                            const auto v = vec[static_cast<std::size_t>(i)];
                            if (v <= static_cast<std::int64_t>(NA_INTEGER) || v > 2147483647LL) {
                                fits_int32 = false;
                                break;
                            }
                        }
                    }
                    if (fits_int32) {
                        SEXP res = PROTECT(cpp11::safe[Rf_allocVector](INTSXP, nrows));
                        int* ip = INTEGER(res);
                        for (R_xlen_t i = 0; i < nrows; ++i) {
                            ip[i] = df.is_null(ci, static_cast<std::size_t>(i))
                                        ? NA_INTEGER
                                        : static_cast<int>(vec[static_cast<std::size_t>(i)]);
                        }
                        UNPROTECT(1);
                        return res;
                    }
                    SEXP res = PROTECT(cpp11::safe[Rf_allocVector](REALSXP, nrows));
                    double* rp = REAL(res);
                    for (R_xlen_t i = 0; i < nrows; ++i) {
                        rp[i] = df.is_null(ci, static_cast<std::size_t>(i))
                                    ? NA_REAL
                                    : static_cast<double>(vec[static_cast<std::size_t>(i)]);
                    }
                    UNPROTECT(1);
                    return res;
                } else if constexpr (std::is_same_v<VecT, std::vector<std::int32_t>>) {
                    SEXP res = PROTECT(type_caster<std::vector<std::int32_t>>::to_r(
                        vec, rv_policy::copy, R_NilValue));
                    if (has_null) {
                        int* ip = INTEGER(res);
                        for (R_xlen_t i = 0; i < nrows; ++i) {
                            if (df.is_null(ci, static_cast<std::size_t>(i))) {
                                ip[i] = NA_INTEGER;
                            }
                        }
                    }
                    UNPROTECT(1);
                    return res;
                } else if constexpr (std::is_same_v<VecT, std::vector<bool>>) {
                    SEXP res = PROTECT(
                        type_caster<std::vector<bool>>::to_r(vec, rv_policy::copy, R_NilValue));
                    if (has_null) {
                        int* lp = LOGICAL(res);
                        for (R_xlen_t i = 0; i < nrows; ++i) {
                            if (df.is_null(ci, static_cast<std::size_t>(i))) {
                                lp[i] = NA_LOGICAL;
                            }
                        }
                    }
                    UNPROTECT(1);
                    return res;
                } else {
                    if (!has_null) {
                        return type_caster<std::vector<std::string>>::to_r(vec, rv_policy::copy,
                                                                           R_NilValue);
                    }
                    SEXP res = PROTECT(cpp11::safe[Rf_allocVector](STRSXP, nrows));
                    for (R_xlen_t i = 0; i < nrows; ++i) {
                        const std::size_t ri = static_cast<std::size_t>(i);
                        if (df.is_null(ci, ri)) {
                            SET_STRING_ELT(res, i, NA_STRING);
                        } else {
                            SEXP ch = PROTECT(cpp11::unwind_protect([&]() {
                                return Rf_mkCharLenCE(vec[ri].data(),
                                                      static_cast<int>(vec[ri].size()), CE_UTF8);
                            }));
                            SET_STRING_ELT(res, i, ch);
                            UNPROTECT(1);
                        }
                    }
                    UNPROTECT(1);
                    return res;
                }
            },
            df.columns[ci]);
        SET_VECTOR_ELT(out, c, col_sexp);
    }

    Rf_setAttrib(out, R_NamesSymbol, col_names);

    SEXP cls = PROTECT(Rf_mkString("data.frame"));
    Rf_setAttrib(out, R_ClassSymbol, cls);

    SEXP row_names = PROTECT(cpp11::safe[Rf_allocVector](INTSXP, 2));
    INTEGER(row_names)[0] = NA_INTEGER;
    INTEGER(row_names)[1] = -static_cast<int>(nrows);
    Rf_setAttrib(out, R_RowNamesSymbol, row_names);

    UNPROTECT(4);
    return out;
}

} // namespace pyrabind::r_detail
