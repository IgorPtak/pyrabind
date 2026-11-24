#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif

#include <R.h>
#include <R_ext/Rdynload.h>
#include <Rinternals.h>

#include "bench_kernels.hpp"

#include <cstdint>
#include <cstring>

namespace {

template <typename FnPtr> DL_FUNC as_dl_func(FnPtr fn) noexcept {
    static_assert(sizeof(FnPtr) == sizeof(DL_FUNC));
    DL_FUNC out = nullptr;
    std::memcpy(&out, &fn, sizeof(out));
    return out;
}

void raw_point_finalizer(SEXP xptr) noexcept {
    auto* ptr = static_cast<pyrabind_bench::BenchPoint*>(R_ExternalPtrAddr(xptr));
    if (ptr != nullptr) {
        delete ptr;
        R_ClearExternalPtr(xptr);
    }
}

SEXP raw_r_noop(void) {
    pyrabind_bench::noop_kernel();
    return R_NilValue;
}

SEXP raw_r_add_i64(SEXP a, SEXP b) {
    const std::int64_t va = (TYPEOF(a) == INTSXP && Rf_xlength(a) == 1)
                                ? static_cast<std::int64_t>(INTEGER_RO(a)[0])
                                : static_cast<std::int64_t>(Rf_asReal(a));
    const std::int64_t vb = (TYPEOF(b) == INTSXP && Rf_xlength(b) == 1)
                                ? static_cast<std::int64_t>(INTEGER_RO(b)[0])
                                : static_cast<std::int64_t>(Rf_asReal(b));
    return Rf_ScalarReal(static_cast<double>(pyrabind_bench::add_i64_kernel(va, vb)));
}

SEXP raw_r_point_new(SEXP x, SEXP y) {
    auto* pt = new pyrabind_bench::BenchPoint(Rf_asReal(x), Rf_asReal(y));
    SEXP xptr = PROTECT(R_MakeExternalPtr(pt, R_NilValue, R_NilValue));
    R_RegisterCFinalizerEx(xptr, &raw_point_finalizer, TRUE);
    UNPROTECT(1);
    return xptr;
}

SEXP raw_r_point_dot(SEXP a, SEXP b) {
    if (TYPEOF(a) != EXTPTRSXP || TYPEOF(b) != EXTPTRSXP) {
        Rf_error("expected external pointer BenchPoint");
    }
    const auto* pa = static_cast<const pyrabind_bench::BenchPoint*>(R_ExternalPtrAddr(a));
    const auto* pb = static_cast<const pyrabind_bench::BenchPoint*>(R_ExternalPtrAddr(b));
    if (pa == nullptr || pb == nullptr) {
        Rf_error("null BenchPoint external pointer");
    }
    return Rf_ScalarReal(pa->dot(*pb));
}

SEXP raw_r_dot_vec(SEXP a, SEXP b) {
    if (TYPEOF(a) != REALSXP || TYPEOF(b) != REALSXP) {
        Rf_error("expected double vectors");
    }
    const R_xlen_t n = Rf_xlength(a);
    if (Rf_xlength(b) != n) {
        Rf_error("vectors must have equal length");
    }
    return Rf_ScalarReal(
        pyrabind_bench::dot_contiguous_kernel(REAL_RO(a), REAL_RO(b), static_cast<std::size_t>(n)));
}

SEXP raw_r_scale_vec(SEXP a, SEXP factor, SEXP offset) {
    if (TYPEOF(a) != REALSXP) {
        Rf_error("expected double vector");
    }
    const R_xlen_t n = Rf_xlength(a);
    const double f = Rf_asReal(factor);
    const double off = Rf_asReal(offset);
    SEXP out = PROTECT(Rf_allocVector(REALSXP, n));
    if (n > 0) {
        pyrabind_bench::scale_contiguous_kernel(REAL_RO(a), REAL(out), static_cast<std::size_t>(n),
                                                f, off);
    }
    UNPROTECT(1);
    return out;
}

SEXP raw_r_mat_sum(SEXP m) {
    if (TYPEOF(m) != REALSXP || !Rf_isMatrix(m)) {
        Rf_error("expected double matrix");
    }
    const R_xlen_t n = Rf_xlength(m);
    return Rf_ScalarReal(
        pyrabind_bench::sum_contiguous_kernel(REAL_RO(m), static_cast<std::size_t>(n)));
}

int find_col_index(SEXP names, const char* target) {
    if (names == R_NilValue || TYPEOF(names) != STRSXP) {
        return -1;
    }
    const R_xlen_t n = Rf_xlength(names);
    for (R_xlen_t i = 0; i < n; ++i) {
        if (std::strcmp(CHAR(STRING_ELT(names, i)), target) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

SEXP raw_r_df_transform(SEXP df) {
    if (TYPEOF(df) != VECSXP) {
        Rf_error("expected data.frame");
    }
    SEXP in_names = Rf_getAttrib(df, R_NamesSymbol);
    const int idx_id = find_col_index(in_names, "id");
    const int idx_x = find_col_index(in_names, "x");
    const int idx_tag = find_col_index(in_names, "tag");
    if (idx_id < 0 || idx_x < 0 || idx_tag < 0) {
        Rf_error("data.frame must contain id, x, tag columns");
    }

    SEXP id_col = VECTOR_ELT(df, idx_id);
    SEXP x_col = VECTOR_ELT(df, idx_x);
    SEXP tag_col = VECTOR_ELT(df, idx_tag);
    if (TYPEOF(x_col) != REALSXP) {
        Rf_error("column 'x' must be double");
    }

    const R_xlen_t n = Rf_xlength(x_col);
    SEXP y_col = PROTECT(Rf_allocVector(REALSXP, n));
    const double* xp = REAL_RO(x_col);
    double* yp = REAL(y_col);

    if (TYPEOF(id_col) == INTSXP) {
        const int* ip = INTEGER_RO(id_col);
        for (R_xlen_t i = 0; i < n; ++i) {
            yp[i] = xp[i] * 2.0 + static_cast<double>(ip[i]);
        }
    } else if (TYPEOF(id_col) == REALSXP) {
        const double* ip = REAL_RO(id_col);
        for (R_xlen_t i = 0; i < n; ++i) {
            yp[i] = xp[i] * 2.0 + ip[i];
        }
    } else {
        UNPROTECT(1);
        Rf_error("column 'id' must be integer or numeric");
    }

    SEXP out = PROTECT(Rf_allocVector(VECSXP, 4));
    SET_VECTOR_ELT(out, 0, id_col);
    SET_VECTOR_ELT(out, 1, x_col);
    SET_VECTOR_ELT(out, 2, y_col);
    SET_VECTOR_ELT(out, 3, tag_col);

    SEXP out_names = PROTECT(Rf_allocVector(STRSXP, 4));
    SET_STRING_ELT(out_names, 0, Rf_mkChar("id"));
    SET_STRING_ELT(out_names, 1, Rf_mkChar("x"));
    SET_STRING_ELT(out_names, 2, Rf_mkChar("y"));
    SET_STRING_ELT(out_names, 3, Rf_mkChar("tag"));
    Rf_setAttrib(out, R_NamesSymbol, out_names);

    SEXP cls = PROTECT(Rf_mkString("data.frame"));
    Rf_setAttrib(out, R_ClassSymbol, cls);

    SEXP row_names = PROTECT(Rf_allocVector(INTSXP, 2));
    INTEGER(row_names)[0] = NA_INTEGER;
    INTEGER(row_names)[1] = -static_cast<int>(n);
    Rf_setAttrib(out, R_RowNamesSymbol, row_names);

    UNPROTECT(5);
    return out;
}

} // namespace

extern "C" {

#if defined(_WIN32)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
void R_init_raw_r_bench(DllInfo* dll) {
    static R_CallMethodDef call_methods[] = {
        {"raw_r_noop", nullptr, 0},      {"raw_r_add_i64", nullptr, 2},
        {"raw_r_point_new", nullptr, 2}, {"raw_r_point_dot", nullptr, 2},
        {"raw_r_dot_vec", nullptr, 2},   {"raw_r_scale_vec", nullptr, 3},
        {"raw_r_mat_sum", nullptr, 1},   {"raw_r_df_transform", nullptr, 1},
        {nullptr, nullptr, 0},
    };
    call_methods[0].fun = as_dl_func(&raw_r_noop);
    call_methods[1].fun = as_dl_func(&raw_r_add_i64);
    call_methods[2].fun = as_dl_func(&raw_r_point_new);
    call_methods[3].fun = as_dl_func(&raw_r_point_dot);
    call_methods[4].fun = as_dl_func(&raw_r_dot_vec);
    call_methods[5].fun = as_dl_func(&raw_r_scale_vec);
    call_methods[6].fun = as_dl_func(&raw_r_mat_sum);
    call_methods[7].fun = as_dl_func(&raw_r_df_transform);

    R_registerRoutines(dll, nullptr, call_methods, nullptr, nullptr);
    R_useDynamicSymbols(dll, FALSE);
}

} // extern "C"
