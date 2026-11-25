<p align="center">
  <img src="docs/src/assets/logo.svg" alt="pyrabind logo -- Poznan potato (pyra) embracing Python and R" width="180">
</p>

<h1 align="center">pyrabind</h1>

<p align="center">
  <a href="https://github.com/IgorPtak/pyrabind/actions/workflows/ci.yml"><img src="https://github.com/IgorPtak/pyrabind/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="https://igorptak.github.io/pyrabind/"><img src="https://github.com/IgorPtak/pyrabind/actions/workflows/docs.yml/badge.svg" alt="Docs"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-BSD_3--Clause-blue.svg" alt="License: BSD-3-Clause"></a>
</p>

<p align="center">
  Write one <strong>C++20</strong> <code>nanobind</code>-style binding module — compile simultaneously for <strong>Python 3</strong> and <strong>R 4</strong>.<br>
  <em>Named after <strong>pyra</strong>, the Poznan dialect word for potato — plus <strong>Py</strong>thon + <strong>R</strong> + <strong>bind</strong>.</em><br>
  <a href="https://igorptak.github.io/pyrabind/">Documentation Site</a>
</p>

`pyrabind` is a **C++20** dual-target binding library that lets you write **one binding definition** in authentic **`nanobind`** syntax (with zero target `#ifdef`s) and compile it simultaneously into:

- a **native Python 3 extension** (`PyInit_<name>`) powered directly by upstream **`nanobind`** (`using namespace ::nanobind` — zero wrapper layer, zero overhead over hand-written `nanobind`), and
- a **native R 4 shared library** (`R_init_<name>`) powered by **`cpp11`**, registered `.External` dispatch, and pre-indexed R environments ([`r/pyrabind.R`](r/pyrabind.R) — zero third-party runtime CRAN dependencies).

---

## Why `pyrabind`?

- **One source of truth**: Expose C++ functions, keyword-only args (`nb::kw_only`), call guards (`nb::call_guard` / `gil_scoped_release`), classes, single inheritance, static properties, overloaded methods, full arithmetic/bitwise/comparison operators, enums, custom C++ exceptions (`nb::exception<T>`), smart pointers (`shared_ptr`, `unique_ptr`), `std::complex`, `std::chrono`, `std::list`, STL containers, iterators (`make_iterator`), opaque container bindings (`PYRABIND_MAKE_OPAQUE`, `bind_vector`, `bind_map`), callbacks, `ndarray` views, **Eigen** dense/`Map`/`Ref` types, nullable `data_frame` tables, and **zero-copy Arrow C Data Interface** batches (`arrow_batch`) from a single `PYRABIND_MODULE` file.
- **Zero-overhead Python backend**: `pyrabind` compiles straight down to upstream `nanobind` on Python (`0.96x–1.05x` of raw `nanobind` dispatch and numerical workloads in `-O3` benchmarks).
- **High-performance safe R backend**: R calls enter through `.External` with stack-allocated `call_args` (avoiding per-call R list allocation), pre-hashed class method/property environments, `cpp11::unwind_protect`, and structured S3 conditions (`pyrabind_*_error`).
- **NumPy / pandas / Eigen / Arrow & base R ready**: Zero-copy strided `nb::ndarray` and `pyrabind/eigen.hpp` views over NumPy buffers and R vectors/matrices, ownership transfer via `nb::capsule`, nullable `nb::data_frame` casters (`NA` / `None` / `NaN` / `pd.NA`), and `pyrabind/arrow.hpp` (`pyarrow` `__arrow_c_array__` PyCapsule in Python, `nanoarrow` in R).
- **Tooling, Packaging & Cross-Platform CI**: Generates `.pyi` type stubs and `roxygen2` R wrappers via `tools/pyrabind_stubgen.py`, ships a complete dual-target `scikit-build-core` Python wheel + `R CMD INSTALL` package template in [`examples/pyra_stats`](examples/pyra_stats), and tests R `gctorture(TRUE)`, `asan-ubsan`, Linux, macOS, and Windows (`MSVC` Python + `Rtools` R).

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
| `pyrabind/pyrabind.hpp` | `<nanobind/nanobind.h>` + `string.h` + `string_view.h` | `module_`, `class_`, `enum_`, `exception<T>`, `PYRABIND_MAKE_OPAQUE`, `PYRABIND_MODULE`, S3 registry |
| `pyrabind/make_iterator.hpp` | `<nanobind/make_iterator.h>` | `make_iterator`, `make_key_iterator`, `make_value_iterator`, `pyrabind_iterator` S3 class |
| `pyrabind/ndarray.hpp` | `<nanobind/ndarray.h>` (NumPy/DLPack) | Zero-copy R vectors/matrices + `capsule` return |
| `pyrabind/eigen.hpp` | `<nanobind/eigen/dense.h>` | `Eigen::Matrix`/`Array`/`Map`/`Ref` + `DRef`/`DMap`/`DStride`/`DRef1`/`DMap1`/`DStride1` |
| `pyrabind/dataframe.hpp` | Nullable `pandas.DataFrame` / `dict` caster (`validity`) | Nullable native `data.frame` / `tibble` caster (`NA` masks) |
| `pyrabind/arrow.hpp` & `arrow_abi.hpp` | `__arrow_c_array__` PyCapsule (`pyarrow` interop) | `nanoarrow_array` / `nanoarrow_schema` externalptr (`nanoarrow` interop) |
| `pyrabind/operators.hpp` | `<nanobind/operators.h>` | `nb::self` expression templates (`+ - * / %% & \| unary + - ! ~ abs hash`) & `Ops` S3 dispatch |
| `pyrabind/stl.hpp` & `pyrabind/stl/*.hpp` | `<nanobind/stl/*.h>` | 21 STL/smart-pointer/chrono/complex/container headers (`bind_vector`, `bind_map`, atomic/list/functional casters) |

---

## Headline `-O3` Benchmark Summary

Measured on Apple M4 (`macOS-27.0.1-arm64-arm-64bit`), Python `3.12.8`, `nanobind 3.1.0`, NumPy `2.4.4`, pandas `3.0.2`, R `4.5.3`, `cpp11 0.5.2`:

| Workload | C++ `-O3` | Python `pyrabind` | Raw `nanobind` | `pyrabind` / `nb` | R `pyrabind` | R raw `.Call` |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| `noop()` | 0.2 ns | **15.9 ns** | 16.8 ns | **0.95x** | 314.4 ns | 112.5 ns |
| `add_i64(17, 25)` | 0.2 ns | **34.2 ns** | 34.4 ns | **0.99x** | 412.3 ns | 145.2 ns |
| `BenchPoint.dot` | 0.7 ns | **38.3 ns** | 37.9 ns | **1.01x** | 2.32 us (`488.5 ns` cached) | 191.6 ns |
| `dot_100k` (`ndarray`) | 55.26 us | **51.41 us** | 51.04 us | **1.01x** | **52.23 us** | 52.04 us |
| `scale_100k` (`ndarray`) | 17.54 us | **13.71 us** | 13.56 us | **1.01x** | 31.45 us | 16.45 us |
| `mat_sum_256` (`ndarray`) | 34.59 us | **32.57 us** | 32.63 us | **1.00x** | **33.26 us** | 33.06 us |
| `df_transform_100k` | 138.51 us | 5.307 ms | 359.03 us (pass-through) | 14.78x | 2.112 ms | 19.60 us |

See [Benchmarks (Starlight)](https://igorptak.github.io/pyrabind/benchmarks/) ([source](docs/src/content/docs/benchmarks.mdx)) for the complete dataset, why eager `data_frame` materialization differs from pass-through baselines, raw CPython C-API baselines, and reproduction instructions.

---

## Building & Testing

```bash
cmake --preset release
cmake --build build-release -j
ctest --test-dir build-release --output-on-failure
cmake --install build-release --prefix /tmp/pyrabind_install

# Address & UndefinedBehavior Sanitizers (C++ core tests)
cmake --preset asan-ubsan
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure

# Generate .pyi type stubs and optional roxygen2 R wrappers
python3 tools/pyrabind_stubgen.py \
  --module pyrastats \
  --python-path build-release/examples/pyra_stats/python \
  --pyi-out examples/pyra_stats/python/pyra_stats/pyrastats.pyi \
  --verify
```

Dual-target packaging examples live in [`examples/geometry`](examples/geometry) (CMake-based minimal quickstart) and [`examples/pyra_stats`](examples/pyra_stats) (`scikit-build-core` Python wheel + native `R CMD INSTALL` package in [`examples/pyra_stats/rpkg`](examples/pyra_stats/rpkg) with `testthat`).

---

## Documentation

- [Documentation Home](https://igorptak.github.io/pyrabind/) ([source](docs/src/content/docs/index.mdx)) — guide map and Python/R feature-parity matrix
- [Getting Started & Build Guide](https://igorptak.github.io/pyrabind/getting-started/) ([source](docs/src/content/docs/getting-started.mdx)) — prerequisites, CMake options, installing, stubgen, dual Python/R packaging
- [API Reference](https://igorptak.github.io/pyrabind/api-reference/) ([source](docs/src/content/docs/api-reference.mdx)) — functions, `kw_only`, `call_guard`, classes, inheritance, static/instance properties, operators, iterators, `bind_vector`/`bind_map`, enums, lifetimes, custom exceptions
- [Type Conversions & Python/R Semantics](https://igorptak.github.io/pyrabind/type-conversions/) ([source](docs/src/content/docs/type-conversions.mdx)) — scalars, all 21 STL/container/smart-pointer headers (`stl.hpp` + `bind_vector`/`bind_map`), `complex`, `chrono`, callbacks, portability differences
- [NumPy, `ndarray`, Eigen, Arrow & `data_frame`](https://igorptak.github.io/pyrabind/ndarray-and-dataframes/) ([source](docs/src/content/docs/ndarray-and-dataframes.mdx)) — zero-copy arrays, column-major R layout, Eigen dense/`Map`/`Ref`, nullable DataFrames, Arrow C Data Interface
- [Architecture Deep Dive](https://igorptak.github.io/pyrabind/architecture/) ([source](docs/src/content/docs/architecture.mdx)) — Python pass-through and R `cpp11` / `.External` / hashed-environment internals
- [Overhead & Data-Science Benchmarks](https://igorptak.github.io/pyrabind/benchmarks/) ([source](docs/src/content/docs/benchmarks.mdx)) — full Release `-O3` benchmark results
- [Agent Guidelines](AGENTS.md), [R Backend Invariants](include/pyrabind/r/AGENTS.md), and [Contributing Guide](CONTRIBUTING.md)

Preview or build the **Astro Starlight** documentation site locally:

```bash
cd docs
npm ci
npm run dev

# Production static build:
npm run build
```
