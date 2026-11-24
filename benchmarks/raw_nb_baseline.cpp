#include "bench_kernels.hpp"

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/vector.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nb = nanobind;

namespace {

struct RawBenchPoint {
    double x = 0.0;
    double y = 0.0;

    RawBenchPoint() = default;
    RawBenchPoint(double x_val, double y_val) noexcept : x(x_val), y(y_val) {}

    double dot(const RawBenchPoint& other) const noexcept { return x * other.x + y * other.y; }
};

void noop() {
    nanorbind_bench::noop_kernel();
}

std::int64_t add_i64(std::int64_t a, std::int64_t b) {
    return nanorbind_bench::add_i64_kernel(a, b);
}

double dot_view(nb::ndarray<const double, nb::ndim<1>> a,
                nb::ndarray<const double, nb::ndim<1>> b) {
    const std::size_t n = a.shape(0);
    if (b.shape(0) != n) {
        throw nb::value_error("dot_view requires arrays of equal length");
    }
    if (a.stride(0) == 1 && b.stride(0) == 1 && n > 0) {
        return nanorbind_bench::dot_contiguous_kernel(a.data(), b.data(), n);
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
    return nanorbind_bench::dot_contiguous_kernel(a.data(), b.data(), n);
}

nb::ndarray<nb::numpy, double, nb::ndim<1>> scale_ndarray(nb::ndarray<const double, nb::ndim<1>> a,
                                                          double factor, double offset) {
    const std::size_t n = a.shape(0);
    auto* buf = new double[n];
    if (a.stride(0) == 1 && n > 0) {
        nanorbind_bench::scale_contiguous_kernel(a.data(), buf, n, factor, offset);
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
        nanorbind_bench::scale_contiguous_kernel(a.data(), out.data(), n, factor, offset);
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
        return nanorbind_bench::sum_contiguous_kernel(m.data(), rows * cols);
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

nb::object df_transform(nb::object df) {
    nb::object id_col = df["id"];
    nb::object x_col = df["x"];
    nb::object tag_col = df["tag"];

    auto id_arr = nb::cast<nb::ndarray<const std::int64_t, nb::ndim<1>>>(id_col.attr("to_numpy")());
    auto x_arr = nb::cast<nb::ndarray<const double, nb::ndim<1>>>(x_col.attr("to_numpy")());
    const std::size_t n = x_arr.shape(0);
    if (id_arr.shape(0) != n) {
        throw nb::value_error("column length mismatch");
    }

    auto* y_buf = new double[n];
    if (id_arr.stride(0) == 1 && x_arr.stride(0) == 1 && n > 0) {
        const std::int64_t* ip = id_arr.data();
        const double* xp = x_arr.data();
        for (std::size_t i = 0; i < n; ++i) {
            y_buf[i] = xp[i] * 2.0 + static_cast<double>(ip[i]);
        }
    } else {
        auto vid = id_arr.view();
        auto vx = x_arr.view();
        for (std::size_t i = 0; i < n; ++i) {
            y_buf[i] = vx(i) * 2.0 + static_cast<double>(vid(i));
        }
    }

    nb::capsule owner(y_buf, [](void* p) noexcept { delete[] static_cast<double*>(p); });
    nb::ndarray<nb::numpy, double, nb::ndim<1>> y_arr(y_buf, {n}, owner);

    nb::dict out;
    out["id"] = id_col;
    out["x"] = x_col;
    out["y"] = nb::cast(std::move(y_arr));
    out["tag"] = tag_col;
    return nb::module_::import_("pandas").attr("DataFrame")(out);
}

} // namespace

NB_MODULE(raw_nb_bench, m) {
    using namespace nb::literals;
    m.doc() = "Raw upstream nanobind benchmark baseline";

    nb::class_<RawBenchPoint>(m, "BenchPoint")
        .def(nb::init<double, double>(), "x"_a, "y"_a)
        .def_rw("x", &RawBenchPoint::x)
        .def_rw("y", &RawBenchPoint::y)
        .def("dot", &RawBenchPoint::dot, "other"_a);

    m.def("noop", &noop);
    m.def("add_i64", &add_i64, "a"_a, "b"_a);
    m.def("dot_view", &dot_view, "a"_a, "b"_a);
    m.def("dot_vec", &dot_vec, "a"_a, "b"_a);
    m.def("scale_ndarray", &scale_ndarray, "a"_a, "factor"_a, "offset"_a);
    m.def("scale_vec", &scale_vec, "a"_a, "factor"_a, "offset"_a);
    m.def("mat_sum_view", &mat_sum_view, "m"_a);
    m.def("df_transform", &df_transform, "df"_a);
}
