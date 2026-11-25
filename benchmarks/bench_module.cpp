#include "bench_kernels.hpp"
#include "pyrabind/arrow.hpp"
#include "pyrabind/dataframe.hpp"
#include "pyrabind/pyrabind.hpp"
#include "pyrabind/ndarray.hpp"
#include "pyrabind/stl/vector.hpp"

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

namespace nb = pyrabind;

namespace {

using pyrabind_bench::BenchPoint;

void noop() {
    pyrabind_bench::noop_kernel();
}

std::int64_t add_i64(std::int64_t a, std::int64_t b) {
    return pyrabind_bench::add_i64_kernel(a, b);
}

double dot_view(nb::ndarray<const double, nb::ndim<1>> a,
                nb::ndarray<const double, nb::ndim<1>> b) {
    const std::size_t n = a.shape(0);
    if (b.shape(0) != n) {
        throw nb::value_error("dot_view requires arrays of equal length");
    }
    if (a.stride(0) == 1 && b.stride(0) == 1 && n > 0) {
        return pyrabind_bench::dot_contiguous_kernel(a.data(), b.data(), n);
    }
    auto va = a.view();
    auto vb = b.view();
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        acc += va(i) * vb(i);
    }
    return acc;
}

double dot_vec(const std::vector<double>& a, const std::vector<double>& b) {
    const std::size_t n = a.size();
    if (b.size() != n) {
        throw nb::value_error("dot_vec requires vectors of equal length");
    }
    return pyrabind_bench::dot_contiguous_kernel(a.data(), b.data(), n);
}

nb::ndarray<nb::numpy, double, nb::ndim<1>> scale_ndarray(nb::ndarray<const double, nb::ndim<1>> a,
                                                          double factor, double offset) {
    const std::size_t n = a.shape(0);
    auto* buf = new double[n];
    if (a.stride(0) == 1 && n > 0) {
        pyrabind_bench::scale_contiguous_kernel(a.data(), buf, n, factor, offset);
    } else {
        auto va = a.view();
        for (std::size_t i = 0; i < n; ++i) {
            buf[i] = va(i) * factor + offset;
        }
    }
    nb::capsule owner(buf, [](void* p) noexcept { delete[] static_cast<double*>(p); });
    return nb::ndarray<nb::numpy, double, nb::ndim<1>>(buf, {n}, owner);
}

std::vector<double> scale_vec(const std::vector<double>& a, double factor, double offset) {
    const std::size_t n = a.size();
    std::vector<double> out(n);
    if (n > 0) {
        pyrabind_bench::scale_contiguous_kernel(a.data(), out.data(), n, factor, offset);
    }
    return out;
}

double mat_sum_view(nb::ndarray<const double, nb::ndim<2>> m) {
    const std::size_t rows = m.shape(0);
    const std::size_t cols = m.shape(1);
    const std::int64_t rs = m.stride(0);
    const std::int64_t cs = m.stride(1);
    if ((rs == static_cast<std::int64_t>(cols) && cs == 1) ||
        (rs == 1 && cs == static_cast<std::int64_t>(rows))) {
        return pyrabind_bench::sum_contiguous_kernel(m.data(), rows * cols);
    }
    auto vm = m.view();
    double acc = 0.0;
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < cols; ++c) {
            acc += vm(r, c);
        }
    }
    return acc;
}

nb::data_frame df_transform(nb::data_frame df) {
    const auto idx_id = df.index_of("id");
    const auto idx_x = df.index_of("x");
    const auto idx_tag = df.index_of("tag");
    const auto* x_vec = std::get_if<std::vector<double>>(&df.columns[idx_x]);
    if (x_vec == nullptr) {
        throw nb::type_error("column 'x' must be float64");
    }

    const std::size_t n = x_vec->size();
    std::vector<double> y_vec(n);
    std::visit(
        [&](const auto& vec) {
            using VecT = std::decay_t<decltype(vec)>;
            if constexpr (std::is_same_v<VecT, std::vector<std::string>>) {
                throw nb::type_error("column 'id' must be numeric");
            } else {
                for (std::size_t i = 0; i < n; ++i) {
                    y_vec[i] = (*x_vec)[i] * 2.0 + static_cast<double>(vec[i]);
                }
            }
        },
        df.columns[idx_id]);

    nb::data_frame out;
    out.add_column("id", std::move(df.columns[idx_id]));
    out.add_column("x", std::move(df.columns[idx_x]));
    out.add_column("y", std::move(y_vec));
    out.add_column("tag", std::move(df.columns[idx_tag]));
    return out;
}

nb::arrow_batch arrow_transform(const nb::arrow_batch_view& batch) {
    const std::size_t n = batch.nrows();
    auto sx = batch.column_span<double>("x");
    std::vector<double> y_vec(n);
    if (batch.column_format("id") == "l") {
        auto sid = batch.column_span<std::int64_t>("id");
        for (std::size_t i = 0; i < n; ++i) {
            y_vec[i] = sx[i] * 2.0 + static_cast<double>(sid[i]);
        }
    } else {
        auto sid = batch.column_span<std::int32_t>("id");
        for (std::size_t i = 0; i < n; ++i) {
            y_vec[i] = sx[i] * 2.0 + static_cast<double>(sid[i]);
        }
    }
    return batch.insert_column(2, "y", std::move(y_vec));
}

nb::arrow_batch df_to_arrow(nb::data_frame df) {
    return nb::export_to_arrow(std::move(df));
}

nb::data_frame arrow_to_df(const nb::arrow_batch& batch) {
    return nb::import_from_arrow(batch);
}

} // namespace

PYRABIND_MODULE(bench_module, m) {
    using namespace nb::literals;
    m.doc() = "Dual-target pyrabind overhead benchmark module";

    nb::class_<BenchPoint>(m, "BenchPoint")
        .def(nb::init<double, double>(), "x"_a, "y"_a)
        .def_rw("x", &BenchPoint::x)
        .def_rw("y", &BenchPoint::y)
        .def("dot", &BenchPoint::dot, "other"_a);

    m.def("noop", &noop);
    m.def("add_i64", &add_i64, "a"_a, "b"_a);
    m.def("dot_view", &dot_view, "a"_a, "b"_a);
    m.def("dot_vec", &dot_vec, "a"_a, "b"_a);
    m.def("scale_ndarray", &scale_ndarray, "a"_a, "factor"_a, "offset"_a);
    m.def("scale_vec", &scale_vec, "a"_a, "factor"_a, "offset"_a);
    m.def("mat_sum_view", &mat_sum_view, "m"_a);
    m.def("df_transform", &df_transform, "df"_a);
    m.def("arrow_transform", &arrow_transform, "batch"_a);
    m.def("df_to_arrow", &df_to_arrow, "df"_a);
    m.def("arrow_to_df", &arrow_to_df, "batch"_a);
}
