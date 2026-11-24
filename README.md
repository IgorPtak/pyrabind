# nanorbind

`nanorbind` is a **C++20** dual-target binding library that lets you write **one binding definition** in authentic **`nanobind`** syntax (with zero `#ifdef`s) and compile it simultaneously into:

- a **native Python 3 extension** (`PyInit_<name>`) powered directly by upstream **`nanobind`** (`using namespace ::nanobind`, zero wrapper overhead), and
- a **native R 4 shared library** (`R_init_<name>`) powered by **`cpp11`**, registered `.External` dispatch, and pre-indexed R environments ([`r/nanorbind.R`](r/nanorbind.R), zero third-party runtime CRAN dependencies).

## Quickstart

### 1. Write one C++20 file (`geometry.cpp`)

```cpp
#include "nanorbind/nanorbind.hpp"
#include "nanorbind/ndarray.hpp"
#include "nanorbind/operators.hpp"
#include <cmath>

namespace nb = nanorbind;

struct Vec2 {
    double x = 0.0, y = 0.0;
    Vec2() = default;
    Vec2(double xv, double yv) : x(xv), y(yv) {}
    double norm() const { return std::hypot(x, y); }
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
};
inline Vec2 operator*(double s, const Vec2& v) { return v * s; }

static double array_dot(nb::ndarray<const double, nb::ndim<1>> a,
                        nb::ndarray<const double, nb::ndim<1>> b) {
    auto va = a.view();
    auto vb = b.view();
    double acc = 0.0;
    for (std::size_t i = 0; i < va.shape(0); ++i) {
        acc += va(i) * vb(i);
    }
    return acc;
}

NANORBIND_MODULE(geometry, m) {
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
```

### 2. Build both targets with CMake

```cmake
nanorbind_add_module(geometry SOURCES geometry.cpp)
```

This emits `geometry<EXT_SUFFIX>` under `python/` and `geometry.so` under `r/`.

### 3. Call identically from Python and R

**Python**
```python
import numpy as np
import geometry as geo

c = 2.0 * geo.Vec2(1.0, 2.0) + geo.Vec2(4.0, 6.0)
print(c.x, c.y, c.norm)
print(geo.array_dot(np.array([1.0, 2.0, 3.0]), np.array([4.0, 5.0, 6.0])))
```

**R**
```r
source("r/nanorbind.R")
geo <- nanorbind_load("build-release/examples/geometry/r/geometry.so")

c <- 2.0 * geo$Vec2(1.0, 2.0) + geo$Vec2(4.0, 6.0)
print(c(c$x, c$y, c$norm))
print(geo$array_dot(c(1.0, 2.0, 3.0), c(4.0, 5.0, 6.0)))
```

## Modular Header Layout

| Header | Python Backend | R Backend |
| :--- | :--- | :--- |
| `nanorbind/nanorbind.hpp` | `<nanobind/nanobind.h>` + `string.h` | `cpp11` + `.External` runtime & S3 registry |
| `nanorbind/ndarray.hpp` | `<nanobind/ndarray.h>` (NumPy/DLPack) | Zero-copy R vectors/matrices + `capsule` return |
| `nanorbind/dataframe.hpp` | `pandas.DataFrame` / `dict` caster | Native `data.frame` / `tibble` caster |
| `nanorbind/operators.hpp` | `<nanobind/operators.h>` | `nb::self` expression templates & `Ops` S3 dispatch |
| `nanorbind/stl/*.hpp`, `stl.hpp` | `<nanobind/stl/*.h>` | R atomic/list/functional casters |

## Documentation & Building

- [Architecture Overview](docs/architecture.md)
- [Overhead & Data-Science Benchmarks](docs/benchmarks.md)
- [Agent Guidelines](AGENTS.md) & [R Backend Invariants](include/nanorbind/r/AGENTS.md)
- [Contributing Guide](CONTRIBUTING.md)

```bash
cmake --preset release
cmake --build build-release -j
ctest --test-dir build-release --output-on-failure
cmake --install build-release --prefix /tmp/nanorbind_install
```
