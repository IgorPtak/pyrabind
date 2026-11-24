# pyrabind

`pyrabind` is a **C++20** dual-target binding library that lets you write **one binding definition** in authentic **`nanobind`** syntax (with zero target `#ifdef`s) and compile it simultaneously into:

- a **native Python 3 extension** (`PyInit_<name>`) powered directly by upstream **`nanobind`** (`using namespace ::nanobind` — zero wrapper layer, zero overhead over hand-written `nanobind`), and
- a **native R 4 shared library** (`R_init_<name>`) powered by **`cpp11`**, registered `.External` dispatch, and pre-indexed R environments ([`r/pyrabind.R`](r/pyrabind.R) — zero third-party runtime CRAN dependencies).

---

## Why `pyrabind`?

- **One source of truth**: Expose C++ functions, classes, single inheritance, overloaded methods, operators, enums, STL containers, callbacks, `ndarray` views, and `data_frame` tables to both Python and R from a single `PYRABIND_MODULE` file.
- **Zero-overhead Python backend**: `pyrabind` compiles straight down to upstream `nanobind` on Python (`0.96x–1.05x` of raw `nanobind` dispatch and numerical workloads in `-O3` benchmarks).
- **High-performance safe R backend**: R calls enter through `.External` with stack-allocated `call_args` (avoiding per-call R list allocation), pre-hashed class method/property environments, `cpp11::unwind_protect`, and structured S3 conditions (`pyrabind_*_error`).
- **NumPy / pandas & base R ready**: Zero-copy strided `nb::ndarray` views over NumPy buffers and R vectors/matrices, ownership transfer via `nb::capsule`, and built-in `nb::data_frame` casters for `pandas.DataFrame` / `dict` and R `data.frame` / `tibble`.

---

## Quickstart

### 1. Write one C++20 file (`geometry.cpp`)

```cpp
#include "pyrabind/pyrabind.hpp"
#include "pyrabind/ndarray.hpp"
#include "pyrabind/operators.hpp"
#include <cmath>
#include <cstddef>

namespace nb = pyrabind;

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
```

### 2. Build both targets with CMake

```cmake
pyrabind_add_module(geometry SOURCES geometry.cpp)
```

This emits `geometry<EXT_SUFFIX>` under `python/` and `geometry.so` (or `.dll`/`.dylib`) under `r/`.

### 3. Call from Python and R

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
source("r/pyrabind.R")
geo <- pyrabind_load("build-release/examples/geometry/r/geometry.so")

c <- 2.0 * geo$Vec2(1.0, 2.0) + geo$Vec2(4.0, 6.0)
print(c(c$x, c$y, c$norm))
print(geo$array_dot(c(1.0, 2.0, 3.0), c(4.0, 5.0, 6.0)))
```

---

## Modular Header Layout

| Header | Python Backend (`nanobind`) | R Backend (`cpp11` + `.External`) |
| :--- | :--- | :--- |
| `pyrabind/pyrabind.hpp` | `<nanobind/nanobind.h>` + `string.h` + `string_view.h` | `module_`, `class_`, `enum_`, `PYRABIND_MODULE`, S3 registry |
| `pyrabind/ndarray.hpp` | `<nanobind/ndarray.h>` (NumPy/DLPack) | Zero-copy R vectors/matrices + `capsule` return |
| `pyrabind/dataframe.hpp` | `pandas.DataFrame` / `dict` caster | Native `data.frame` / `tibble` caster |
| `pyrabind/operators.hpp` | `<nanobind/operators.h>` | `nb::self` expression templates & `Ops` S3 dispatch |
| `pyrabind/stl.hpp` & `pyrabind/stl/*.hpp` | `<nanobind/stl/*.h>` | R atomic/list/functional casters |

---

## Headline `-O3` Benchmark Summary

Measured on Apple M4 (`macOS-27.0.1-arm64-arm-64bit`), Python `3.12.8`, `nanobind 3.1.0`, NumPy `2.4.4`, pandas `3.0.2`, R `4.5.3`, `cpp11 0.5.2`:

| Workload | C++ `-O3` | Python `pyrabind` | Raw `nanobind` | `pyrabind` / `nb` | R `pyrabind` | R raw `.Call` |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| `noop()` | 0.5 ns | **15.8 ns** | 16.5 ns | **0.96x** | 311.3 ns | 115.5 ns |
| `add_i64(17, 25)` | 0.4 ns | **33.1 ns** | 33.2 ns | **1.00x** | 408.7 ns | 146.9 ns |
| `BenchPoint.dot` | 1.2 ns | **38.4 ns** | 36.7 ns | **1.05x** | 2.36 us (`487.2 ns` cached) | 188.8 ns |
| `dot_100k` (`ndarray`) | 51.53 us | **51.72 us** | 51.73 us | **1.00x** | **52.98 us** | 52.66 us |
| `scale_100k` (`ndarray`) | 16.63 us | **10.21 us** | 13.79 us | **0.74x** | 30.05 us | 16.30 us |
| `mat_sum_256` (`ndarray`) | 33.10 us | **32.85 us** | 32.83 us | **1.00x** | **32.94 us** | 32.61 us |
| `df_transform_100k` | 135.47 us | 3.539 ms | 346.19 us (pass-through) | 10.22x | 1.970 ms | 19.72 us |

See [docs/benchmarks.md](docs/benchmarks.md) for the complete dataset, `std::vector` copy numbers, raw CPython C-API baselines, and reproduction instructions.

---

## Building & Testing

```bash
cmake --preset release
cmake --build build-release -j
ctest --test-dir build-release --output-on-failure
cmake --install build-release --prefix /tmp/pyrabind_install
```

---

## Documentation

- [Documentation Home](docs/index.md) — guide map and Python/R feature-parity matrix
- [Getting Started & Build Guide](docs/getting-started.md) — prerequisites, CMake options, installing, external projects
- [API Reference](docs/api-reference.md) — functions, classes, inheritance, properties, operators, enums, lifetimes, exceptions
- [Type Conversions & Python/R Semantics](docs/type-conversions.md) — scalars, all 14 STL headers, callbacks, portability differences
- [NumPy, `ndarray`, Matrices & `data_frame`](docs/ndarray-and-dataframes.md) — zero-copy arrays, column-major R layout, DataFrames
- [Architecture Deep Dive](docs/architecture.md) — Python pass-through and R `cpp11` / `.External` / hashed-environment internals
- [Overhead & Data-Science Benchmarks](docs/benchmarks.md) — full Release `-O3` benchmark results
- [Agent Guidelines](AGENTS.md), [R Backend Invariants](include/pyrabind/r/AGENTS.md), and [Contributing Guide](CONTRIBUTING.md)
