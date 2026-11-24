#include "pyrabind/arrow.hpp"
#include "pyrabind/dataframe.hpp"
#include "pyrabind/ndarray.hpp"
#include "pyrabind/pyrabind.hpp"
#include "pyrabind/stl.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace nb = pyrabind;

struct SummaryStats {
    std::size_t n = 0;
    double mean = 0.0;
    double std = 0.0;
    double median = 0.0;
    double mad = 0.0;
    double q25 = 0.0;
    double q75 = 0.0;
};

namespace {

double linear_quantile(const std::vector<double>& sorted, double q) noexcept {
    if (sorted.empty()) {
        return 0.0;
    }
    if (sorted.size() == 1) {
        return sorted[0];
    }
    const double pos = q * static_cast<double>(sorted.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(pos);
    const std::size_t hi = lo + 1 < sorted.size() ? lo + 1 : lo;
    const double frac = pos - static_cast<double>(lo);
    return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

std::vector<double> cholesky_solve(std::size_t p, std::vector<double> a,
                                   const std::vector<double>& b) {
    // Factorize symmetric positive-definite `a` (row-major p x p) = L * L^T.
    std::vector<double> l(p * p, 0.0);
    for (std::size_t i = 0; i < p; ++i) {
        for (std::size_t j = 0; j <= i; ++j) {
            double sum = a[i * p + j];
            for (std::size_t k = 0; k < j; ++k) {
                sum -= l[i * p + k] * l[j * p + k];
            }
            if (i == j) {
                if (sum <= 1e-14) {
                    throw nb::value_error("design matrix is singular; increase alpha");
                }
                l[i * p + j] = std::sqrt(sum);
            } else {
                l[i * p + j] = sum / l[j * p + j];
            }
        }
    }

    // Forward substitution: L * y = b.
    std::vector<double> y(p, 0.0);
    for (std::size_t i = 0; i < p; ++i) {
        double sum = b[i];
        for (std::size_t k = 0; k < i; ++k) {
            sum -= l[i * p + k] * y[k];
        }
        y[i] = sum / l[i * p + i];
    }

    // Backward substitution: L^T * x = y.
    std::vector<double> x(p, 0.0);
    for (std::size_t idx = p; idx > 0; --idx) {
        const std::size_t i = idx - 1;
        double sum = y[i];
        for (std::size_t k = i + 1; k < p; ++k) {
            sum -= l[k * p + i] * x[k];
        }
        x[i] = sum / l[i * p + i];
    }
    return x;
}

SummaryStats compute_robust_summary(nb::ndarray<const double, nb::ndim<1>> x) {
    const std::size_t n = x.shape(0);
    if (n == 0) {
        throw nb::value_error("robust_summary requires at least one finite value");
    }
    auto vx = x.view();
    std::vector<double> sorted;
    sorted.reserve(n);
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double v = vx(i);
        if (std::isfinite(v)) {
            sorted.push_back(v);
            sum += v;
        }
    }
    if (sorted.empty()) {
        throw nb::value_error("robust_summary found no finite values");
    }

    std::sort(sorted.begin(), sorted.end());
    const std::size_t m = sorted.size();
    const double mean_val = sum / static_cast<double>(m);
    double ss = 0.0;
    for (double v : sorted) {
        const double d = v - mean_val;
        ss += d * d;
    }
    const double std_val = m > 1 ? std::sqrt(ss / static_cast<double>(m - 1)) : 0.0;
    const double med = linear_quantile(sorted, 0.5);

    std::vector<double> abs_dev(m);
    for (std::size_t i = 0; i < m; ++i) {
        abs_dev[i] = std::abs(sorted[i] - med);
    }
    std::sort(abs_dev.begin(), abs_dev.end());
    const double mad_val = linear_quantile(abs_dev, 0.5) * 1.482602218505602;

    return SummaryStats{m,
                        mean_val,
                        std_val,
                        med,
                        mad_val,
                        linear_quantile(sorted, 0.25),
                        linear_quantile(sorted, 0.75)};
}

nb::ndarray<nb::numpy, double, nb::ndim<2>>
compute_pairwise_euclidean(nb::ndarray<const double, nb::ndim<2>> a,
                           nb::ndarray<const double, nb::ndim<2>> b) {
    const std::size_t na = a.shape(0);
    const std::size_t nb_rows = b.shape(0);
    const std::size_t dims = a.shape(1);
    if (b.shape(1) != dims) {
        throw nb::value_error("pairwise_euclidean inputs must have the same number of columns");
    }
    auto va = a.view();
    auto vb = b.view();
    auto* out = new double[na * nb_rows];
    {
        nb::gil_scoped_release nogil;
        for (std::size_t i = 0; i < na; ++i) {
            for (std::size_t j = 0; j < nb_rows; ++j) {
                double sq = 0.0;
                for (std::size_t k = 0; k < dims; ++k) {
                    const double diff = va(i, k) - vb(j, k);
                    sq += diff * diff;
                }
                out[i * nb_rows + j] = std::sqrt(sq);
            }
        }
    }
    nb::capsule owner(out, [](void* ptr) noexcept { delete[] static_cast<double*>(ptr); });
    return nb::ndarray<nb::numpy, double, nb::ndim<2>>(out, {na, nb_rows}, owner);
}

class RidgeRegressor {
public:
    explicit RidgeRegressor(double alpha = 1.0) : alpha_(alpha) {
        if (alpha_ < 0.0 || !std::isfinite(alpha_)) {
            throw nb::value_error("alpha must be finite and non-negative");
        }
    }

    double alpha() const noexcept { return alpha_; }
    double intercept() const noexcept { return intercept_; }
    const std::vector<double>& coef() const noexcept { return coef_; }
    bool is_fitted() const noexcept { return fitted_; }

    RidgeRegressor& fit(nb::ndarray<const double, nb::ndim<2>> x,
                        nb::ndarray<const double, nb::ndim<1>> y) {
        const std::size_t n = x.shape(0);
        const std::size_t p = x.shape(1);
        if (n == 0 || p == 0) {
            throw nb::value_error("X must be non-empty");
        }
        if (y.shape(0) != n) {
            throw nb::value_error("y length must match X rows");
        }

        auto vx = x.view();
        auto vy = y.view();

        std::vector<double> x_mean(p, 0.0);
        double y_mean = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            y_mean += vy(i);
            for (std::size_t j = 0; j < p; ++j) {
                x_mean[j] += vx(i, j);
            }
        }
        const double inv_n = 1.0 / static_cast<double>(n);
        y_mean *= inv_n;
        for (std::size_t j = 0; j < p; ++j) {
            x_mean[j] *= inv_n;
        }

        std::vector<double> gram(p * p, 0.0);
        std::vector<double> rhs(p, 0.0);
        std::vector<double> row(p, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            const double yc = vy(i) - y_mean;
            for (std::size_t j = 0; j < p; ++j) {
                row[j] = vx(i, j) - x_mean[j];
                rhs[j] += row[j] * yc;
            }
            for (std::size_t r = 0; r < p; ++r) {
                for (std::size_t c = 0; c <= r; ++c) {
                    gram[r * p + c] += row[r] * row[c];
                }
            }
        }
        for (std::size_t r = 0; r < p; ++r) {
            for (std::size_t c = 0; c < r; ++c) {
                gram[c * p + r] = gram[r * p + c];
            }
            gram[r * p + r] += alpha_;
        }

        std::vector<double> w = cholesky_solve(p, std::move(gram), rhs);
        double bias = y_mean;
        for (std::size_t j = 0; j < p; ++j) {
            bias -= x_mean[j] * w[j];
        }

        coef_ = std::move(w);
        intercept_ = bias;
        fitted_ = true;
        return *this;
    }

    nb::ndarray<nb::numpy, double, nb::ndim<1>>
    predict(nb::ndarray<const double, nb::ndim<2>> x) const {
        if (!fitted_) {
            throw nb::value_error("RidgeRegressor is not fitted yet");
        }
        const std::size_t n = x.shape(0);
        const std::size_t p = x.shape(1);
        if (p != coef_.size()) {
            throw nb::value_error("feature dimension mismatch in predict()");
        }
        auto vx = x.view();
        auto* out = new double[n];
        {
            nb::gil_scoped_release nogil;
            for (std::size_t i = 0; i < n; ++i) {
                double pred = intercept_;
                for (std::size_t j = 0; j < p; ++j) {
                    pred += vx(i, j) * coef_[j];
                }
                out[i] = pred;
            }
        }
        nb::capsule owner(out, [](void* ptr) noexcept { delete[] static_cast<double*>(ptr); });
        return nb::ndarray<nb::numpy, double, nb::ndim<1>>(out, {n}, owner);
    }

    double score_r2(nb::ndarray<const double, nb::ndim<2>> x,
                    nb::ndarray<const double, nb::ndim<1>> y) const {
        if (!fitted_) {
            throw nb::value_error("RidgeRegressor is not fitted yet");
        }
        const std::size_t n = x.shape(0);
        const std::size_t p = x.shape(1);
        if (y.shape(0) != n || p != coef_.size()) {
            throw nb::value_error("dimension mismatch in score_r2()");
        }
        auto vx = x.view();
        auto vy = y.view();
        double y_mean = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            y_mean += vy(i);
        }
        y_mean /= static_cast<double>(n);

        double ss_res = 0.0;
        double ss_tot = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            double pred = intercept_;
            for (std::size_t j = 0; j < p; ++j) {
                pred += vx(i, j) * coef_[j];
            }
            const double err = vy(i) - pred;
            const double tot = vy(i) - y_mean;
            ss_res += err * err;
            ss_tot += tot * tot;
        }
        return ss_tot > 0.0 ? (1.0 - ss_res / ss_tot) : 1.0;
    }

private:
    double alpha_ = 1.0;
    std::vector<double> coef_;
    double intercept_ = 0.0;
    bool fitted_ = false;
};

nb::data_frame standardize_table(const nb::data_frame& df, const std::vector<std::string>& cols) {
    nb::data_frame out = df;
    const std::size_t n = df.nrows();
    for (const std::string& cname : cols) {
        const std::size_t ci = out.index_of(cname);
        std::vector<double> vals(n, 0.0);
        std::visit(
            [&](const auto& src) {
                using VecT = std::decay_t<decltype(src)>;
                if constexpr (std::is_same_v<VecT, std::vector<std::string>>) {
                    throw nb::type_error(
                        ("cannot standardize string column '" + cname + "'").c_str());
                } else {
                    std::size_t valid_cnt = 0;
                    double sum = 0.0;
                    for (std::size_t r = 0; r < n; ++r) {
                        if (!out.is_null(ci, r)) {
                            const double v = static_cast<double>(src[r]);
                            vals[r] = v;
                            sum += v;
                            ++valid_cnt;
                        }
                    }
                    if (valid_cnt == 0) {
                        throw nb::value_error(
                            ("column '" + cname + "' has no non-null values").c_str());
                    }
                    const double mean_v = sum / static_cast<double>(valid_cnt);
                    double ss = 0.0;
                    for (std::size_t r = 0; r < n; ++r) {
                        if (!out.is_null(ci, r)) {
                            const double d = vals[r] - mean_v;
                            ss += d * d;
                        }
                    }
                    const double std_v = std::sqrt(ss / static_cast<double>(valid_cnt));
                    if (std_v == 0.0) {
                        throw nb::value_error(("column '" + cname + "' has zero variance").c_str());
                    }
                    for (std::size_t r = 0; r < n; ++r) {
                        if (!out.is_null(ci, r)) {
                            vals[r] = (vals[r] - mean_v) / std_v;
                        }
                    }
                }
            },
            out.columns[ci]);
        out.columns[ci] = std::move(vals);
    }
    return out;
}

nb::arrow_batch stats_to_arrow(nb::data_frame df) {
    return nb::export_to_arrow(std::move(df));
}

} // namespace

PYRABIND_MODULE(pyrastats, m) {
    using namespace nb::literals;
    m.doc() = "Statistical kernels compiled identically for Python and R via pyrabind";

    nb::class_<SummaryStats>(m, "SummaryStats")
        .def_ro("n", &SummaryStats::n)
        .def_ro("mean", &SummaryStats::mean)
        .def_ro("std", &SummaryStats::std)
        .def_ro("median", &SummaryStats::median)
        .def_ro("mad", &SummaryStats::mad)
        .def_ro("q25", &SummaryStats::q25)
        .def_ro("q75", &SummaryStats::q75);

    nb::class_<RidgeRegressor>(m, "RidgeRegressor")
        .def(nb::init<double>(), nb::kw_only(), "alpha"_a = 1.0)
        .def_prop_ro("alpha", &RidgeRegressor::alpha)
        .def_prop_ro("intercept", &RidgeRegressor::intercept)
        .def_prop_ro("coef", &RidgeRegressor::coef)
        .def_prop_ro("is_fitted", &RidgeRegressor::is_fitted)
        .def("fit", &RidgeRegressor::fit, "X"_a, "y"_a, nb::rv_policy::reference,
             nb::call_guard<nb::gil_scoped_release>())
        .def("predict", &RidgeRegressor::predict, "X"_a)
        .def("score_r2", &RidgeRegressor::score_r2, "X"_a, "y"_a);

    m.def("robust_summary", &compute_robust_summary, "x"_a);
    m.def("pairwise_euclidean", &compute_pairwise_euclidean, "a"_a, "b"_a);
    m.def("standardize_table", &standardize_table, "df"_a, "cols"_a);
    m.def("stats_to_arrow", &stats_to_arrow, "df"_a);
}
