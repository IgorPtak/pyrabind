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
    m.def("scale_ndarray", &scale_ndarray, "a"_a, "factor"_a, "offset"_a);
    m.def("mat_sum_view", &mat_sum_view, "m"_a);
}
