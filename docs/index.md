# pyrabind Documentation

Welcome to the **`pyrabind`** documentation.

`pyrabind` enables C++20 developers to write a single binding definition using upstream `nanobind` idioms and compile native bindings for both **Python 3** (powered directly by `nanobind`) and **R 4** (powered by `cpp11` and base R C routines) without preprocessor branching in user code.

## Guide Map

1. **[Getting Started & Build Guide](getting-started.md)** — dependencies, CMake presets and options, `pyrabind_add_module`, installing and consuming via `find_package(pyrabind)`, and running the `examples/geometry` sample.
2. **[API Reference](api-reference.md)** — modules, submodules, free functions, keyword arguments, defaults, overload resolution, classes, single inheritance, constructors/factories, properties, special methods, operators, enums, ownership (`rv_policy`, `keep_alive`, `capsule`), and exceptions.
3. **[Type Conversions & Python/R Semantics](type-conversions.md)** — scalars, UTF-8 strings, all 14 `pyrabind/stl/*.hpp` headers, bidirectional callbacks (`std::function`), and practical behavioral differences between Python and R callers.
4. **[NumPy, `ndarray`, Matrices & `data_frame`](ndarray-and-dataframes.md)** — zero-copy strided array views, row-major vs. column-major matrices, allocating output arrays with `nb::capsule`, ALTREP considerations, and `nb::data_frame` (`pandas.DataFrame` / `dict` and base R `data.frame` / `tibble`).
5. **[Architecture Deep Dive](architecture.md)** — how `pyrabind` achieves zero Python overhead alongside fast, unwind-safe `.External` R dispatch.
6. **[Overhead & Data-Science Benchmarks](benchmarks.md)** — measured Release `-O3` latency across C++, Python (`pyrabind`, raw `nanobind`, raw CPython C-API, NumPy/pandas), and R (`pyrabind`, raw `.Call`, base R).

---

## Python vs. R Feature Parity Overview

| Capability | Python (`nanobind`) | R (`cpp11` + `.External`) | Notes |
| :--- | :--- | :--- | :--- |
| Free functions & lambdas | Yes | Yes | Identical `m.def(...)` syntax |
| Positional, keyword (`"x"_a`) & default args | Yes | Yes | Two-pass resolution (`convert = false` then `true`) |
| `nb::arg().noconvert()` & `.none()` / `nb::none()` | Yes | Yes | Strict match skips implicit coercion |
| Overloads & `nb::overload_cast` + `nb::const_` | Yes | Yes | Exact integer vs float types in R (`5L` vs `5.0`) |
| `nb::class_<T>` & single base `class_<Derived, Base>` | Yes | Yes | Multiple inheritance is Python-only |
| Constructors (`nb::init<...>`) & factories (`nb::new_`) | Yes | Yes | Shared/managed object lifetime |
| Properties (`def_ro`, `def_rw`, `def_prop_ro`, `def_prop_rw`) | Yes (`obj.prop`) | Yes (`obj$prop`) | Pre-indexed hashed R environments |
| Static methods (`def_static`) | Yes (`Cls.fn()`) | Yes (`Cls$fn()`) | Stored on class `.pb_statics` env in R |
| Special methods (`__repr__`, `__len__`, `__getitem__`, `__setitem__`) | Yes | Yes (`print`/`format`, `length`, `[[`, `[[<-`) | `__contains__`: `v in x` (Py) / `` x$`__contains__`(v) `` (R) |
| Arithmetic & comparison operators (`pyrabind/operators.hpp`) | Yes | Yes (`Ops` S3) | Unary `-`, binary `+ - * /`, comparisons `== != < <= > >=` |
| Enumerations (`nb::enum_<E>`, `.export_values()`) | Yes | Yes (`Enum$Val`, `as.integer`, `==`/`!=`) | Bitwise `Enum \| Enum` is Python-only |
| Lifetime policies (`rv_policy`, `keep_alive<Nurse, Patient>`) | Yes | Yes | Wired through `R_ExternalPtrProtected` in R |
| Typed exceptions (`value_error`, `type_error`, `index_error`, `key_error`, `attribute_error`, `stop_iteration`) | Python exceptions | R S3 conditions (`pyrabind_*_error`) | Catchable with `try`/`except` (Py) or `tryCatch` (R) |
| STL containers (`pyrabind/stl.hpp`) | Full `<nanobind/stl/*.h>` | All 14 headers | Maps use `std::string` keys in R |
| Zero-copy `nb::ndarray` views & `capsule` returns | NumPy / buffer / DLPack | R vectors, matrices, arrays (`dim`) | Row-major C++ 2D outputs auto-transpose to column-major R |
| `nb::data_frame` | `pandas.DataFrame` & `dict` | `data.frame` & `tibble` | `float64`, `int64`, `int32`, `bool`, `std::string` columns |
