#pragma once

#include "nanorbind/r/caster.hpp"

#include <cpp11/R.hpp>
#include <cpp11/protect.hpp>

#include <cstdint>
#include <vector>

namespace nanorbind::r_detail {

// Helper to convert homogeneous R atomic vectors without per-element unwind overhead.
template <typename T> struct type_caster<std::vector<T>> {
    std::vector<T> val;

    bool from_r(SEXP x, bool convert) {
        val.clear();
        if (x == R_NilValue) {
            return false;
        }
        if constexpr (std::is_same_v<T, double> || std::is_same_v<T, float>) {
            const SEXPTYPE tp = static_cast<SEXPTYPE>(TYPEOF(x));
            if (tp == REALSXP) {
                const R_xlen_t n = Rf_xlength(x);
                val.resize(static_cast<std::size_t>(n));
                const double* src = REAL_RO(x);
                if constexpr (std::is_same_v<T, double>) {
                    if (n > 0) {
                        std::memcpy(val.data(), src, static_cast<std::size_t>(n) * sizeof(double));
                    }
                } else {
                    for (R_xlen_t i = 0; i < n; ++i) {
                        val[static_cast<std::size_t>(i)] = static_cast<float>(src[i]);
                    }
                }
                return true;
            }
            if (convert && tp == INTSXP) {
                const R_xlen_t n = Rf_xlength(x);
                val.resize(static_cast<std::size_t>(n));
                const int* src = INTEGER_RO(x);
                for (R_xlen_t i = 0; i < n; ++i) {
                    val[static_cast<std::size_t>(i)] = static_cast<T>(src[i]);
                }
                return true;
            }
        } else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
            const SEXPTYPE tp = static_cast<SEXPTYPE>(TYPEOF(x));
            if (tp == INTSXP) {
                const R_xlen_t n = Rf_xlength(x);
                val.resize(static_cast<std::size_t>(n));
                const int* src = INTEGER_RO(x);
                for (R_xlen_t i = 0; i < n; ++i) {
                    val[static_cast<std::size_t>(i)] = static_cast<T>(src[i]);
                }
                return true;
            }
            if (convert && tp == REALSXP) {
                const R_xlen_t n = Rf_xlength(x);
                val.resize(static_cast<std::size_t>(n));
                const double* src = REAL_RO(x);
                for (R_xlen_t i = 0; i < n; ++i) {
                    val[static_cast<std::size_t>(i)] = static_cast<T>(src[i]);
                }
                return true;
            }
        } else if constexpr (std::is_same_v<T, bool>) {
            if (TYPEOF(x) == LGLSXP) {
                const R_xlen_t n = Rf_xlength(x);
                val.resize(static_cast<std::size_t>(n), false);
                const int* src = LOGICAL_RO(x);
                for (R_xlen_t i = 0; i < n; ++i) {
                    val[static_cast<std::size_t>(i)] = (src[i] != 0 && src[i] != NA_LOGICAL);
                }
                return true;
            }
        }

        if (TYPEOF(x) == VECSXP) {
            const R_xlen_t n = Rf_xlength(x);
            val.reserve(static_cast<std::size_t>(n));
            for (R_xlen_t i = 0; i < n; ++i) {
                type_caster<T> ec;
                if (!ec.from_r(VECTOR_ELT(x, i), convert)) {
                    return false;
                }
                val.push_back(std::move(ec.get()));
            }
            return true;
        }
        if constexpr (std::is_same_v<T, std::string>) {
            if (TYPEOF(x) == STRSXP) {
                const R_xlen_t n = Rf_xlength(x);
                val.resize(static_cast<std::size_t>(n));
                for (R_xlen_t i = 0; i < n; ++i) {
                    SEXP elt = STRING_ELT(x, i);
                    if (elt == NA_STRING) {
                        return false;
                    }
                    const cetype_t ce = Rf_getCharCE(elt);
                    const char* raw = CHAR(elt);
                    const int len = Rf_length(elt);
                    bool ascii = (ce == CE_UTF8);
                    if (!ascii) {
                        ascii = true;
                        for (int b = 0; b < len; ++b) {
                            if (static_cast<unsigned char>(raw[b]) >= 0x80U) {
                                ascii = false;
                                break;
                            }
                        }
                    }
                    if (ascii) {
                        val[static_cast<std::size_t>(i)].assign(raw, static_cast<std::size_t>(len));
                    } else {
                        val[static_cast<std::size_t>(i)] = cpp11::unwind_protect(
                            [&]() { return std::string(Rf_translateCharUTF8(elt)); });
                    }
                }
                return true;
            }
        }
        if (!convert && TYPEOF(x) == VECSXP) {
            return false;
        }
        const R_xlen_t n = Rf_xlength(x);
        if (TYPEOF(x) == REALSXP || TYPEOF(x) == INTSXP || TYPEOF(x) == LGLSXP) {
            if (n == 0) {
                return true;
            }
        }
        return false;
    }

    std::vector<T>& get() noexcept { return val; }

    static SEXP to_r(const std::vector<T>& vec, rv_policy policy, SEXP parent) {
        const R_xlen_t n = static_cast<R_xlen_t>(vec.size());
        if constexpr (std::is_same_v<T, double> || std::is_same_v<T, float>) {
            SEXP out = cpp11::safe[Rf_allocVector](REALSXP, n);
            PROTECT(out);
            double* dst = REAL(out);
            if constexpr (std::is_same_v<T, double>) {
                if (n > 0) {
                    std::memcpy(dst, vec.data(), static_cast<std::size_t>(n) * sizeof(double));
                }
            } else {
                for (R_xlen_t i = 0; i < n; ++i) {
                    dst[i] = static_cast<double>(vec[static_cast<std::size_t>(i)]);
                }
            }
            UNPROTECT(1);
            return out;
        } else if constexpr (std::is_same_v<T, bool>) {
            SEXP out = cpp11::safe[Rf_allocVector](LGLSXP, n);
            PROTECT(out);
            int* dst = LOGICAL(out);
            for (R_xlen_t i = 0; i < n; ++i) {
                dst[i] = vec[static_cast<std::size_t>(i)] ? 1 : 0;
            }
            UNPROTECT(1);
            return out;
        } else if constexpr (std::is_integral_v<T>) {
            bool fits_i32 = true;
            if constexpr (sizeof(T) > 4) {
                for (const auto v : vec) {
                    const auto s = static_cast<std::int64_t>(v);
                    if (s <= static_cast<std::int64_t>(NA_INTEGER) || s > 2147483647LL) {
                        fits_i32 = false;
                        break;
                    }
                }
            }
            if (fits_i32) {
                SEXP out = cpp11::safe[Rf_allocVector](INTSXP, n);
                PROTECT(out);
                int* dst = INTEGER(out);
                for (R_xlen_t i = 0; i < n; ++i) {
                    dst[i] = static_cast<int>(vec[static_cast<std::size_t>(i)]);
                }
                UNPROTECT(1);
                return out;
            }
            SEXP out = cpp11::safe[Rf_allocVector](REALSXP, n);
            PROTECT(out);
            double* dst = REAL(out);
            for (R_xlen_t i = 0; i < n; ++i) {
                dst[i] = static_cast<double>(vec[static_cast<std::size_t>(i)]);
            }
            UNPROTECT(1);
            return out;
        } else if constexpr (std::is_same_v<T, std::string>) {
            SEXP out = cpp11::safe[Rf_allocVector](STRSXP, n);
            PROTECT(out);
            cpp11::unwind_protect([&]() {
                for (R_xlen_t i = 0; i < n; ++i) {
                    const auto& s = vec[static_cast<std::size_t>(i)];
                    SET_STRING_ELT(out, i,
                                   Rf_mkCharLenCE(s.data(), static_cast<int>(s.size()), CE_UTF8));
                }
            });
            UNPROTECT(1);
            return out;
        } else {
            SEXP out = cpp11::safe[Rf_allocVector](VECSXP, n);
            PROTECT(out);
            for (R_xlen_t i = 0; i < n; ++i) {
                SEXP elem = type_caster<T>::to_r(vec[static_cast<std::size_t>(i)], policy, parent);
                SET_VECTOR_ELT(out, i, elem);
            }
            UNPROTECT(1);
            return out;
        }
    }
};

} // namespace nanorbind::r_detail
