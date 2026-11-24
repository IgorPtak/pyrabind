#include "pyrabind/pyrabind.hpp"
#include "pyrabind/ndarray.hpp"
#include "pyrabind/operators.hpp"

#include <cmath>
#include <cstddef>

namespace nb = pyrabind;

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    Vec2() = default;
    Vec2(double xv, double yv) : x(xv), y(yv) {}

    double norm() const { return std::hypot(x, y); }
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
};

inline Vec2 operator*(double s, const Vec2& v) {
    return v * s;
}

static double array_dot(nb::ndarray<const double, nb::ndim<1>> a,
                        nb::ndarray<const double, nb::ndim<1>> b) {
    if (a.shape(0) != b.shape(0)) {
        throw nb::value_error("vectors must have equal length");
    }
    auto va = a.view();
    auto vb = b.view();
    double acc = 0.0;
    for (std::size_t i = 0; i < va.shape(0); ++i) {
        acc += va(i) * vb(i);
    }
    return acc;
}

PYRABIND_MODULE(geometry, m) {
    using namespace nb::literals;
    m.doc() = "2D geometry module shared across Python and R";

    nb::class_<Vec2>(m, "Vec2")
        .def(nb::init<>())
        .def(nb::init<double, double>(), "x"_a, "y"_a)
        .def_rw("x", &Vec2::x)
        .def_rw("y", &Vec2::y)
        .def_prop_ro("norm", &Vec2::norm)
        .def(nb::self + nb::self)
        .def(nb::self * double())
        .def(double() * nb::self);

    m.def("array_dot", &array_dot, "a"_a, "b"_a);
}
