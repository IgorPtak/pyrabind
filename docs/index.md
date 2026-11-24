<p align="center">
  <img src="assets/logo.png" alt="pyrabind logo — Poznan potato (pyra) embracing Python and R" width="160">
</p>

# pyrabind Documentation

Welcome to the **`pyrabind`** documentation (*named after **pyra**, the Poznan dialect word for potato -- plus **Py**thon + **R** + **bind***).

`pyrabind` enables C++20 developers to write a single binding definition using upstream `nanobind` idioms and compile native bindings for both **Python 3** (powered directly by `nanobind`) and **R 4** (powered by `cpp11` and base R C routines) without preprocessor branching in user code.

## Guide Map

1. **[Getting Started & Build Guide](getting-started.md)** — dependencies, CMake presets and options, `pyrabind_add_module`, installing and consuming via `find_package(pyrabind)`, running `examples/geometry`, and building the MkDocs site locally.
2. **[API Reference](api-reference.md)** — modules, submodules, free functions, keyword arguments, `kw_only`, `call_guard`, overload resolution, classes, single inheritance, constructors/factories, instance and static properties, iterators, `bind_vector`/`bind_map`, special methods, operators, enums, ownership (`rv_policy`, `keep_alive`, `capsule`), and built-in/custom `exception<T>` types.
3. **[Type Conversions & Python/R Semantics](type-conversions.md)** — scalars, UTF-8 strings, all 21 `pyrabind/stl/*.hpp` headers (19 via `stl.hpp` plus opt-in `bind_vector.hpp` and `bind_map.hpp`, smart pointers, `complex`, `chrono`, `list`), bidirectional callbacks (`std::function`), and practical behavioral differences between Python and R callers.
4. **[NumPy, `ndarray`, Matrices & `data_frame`](ndarray-and-dataframes.md)** — zero-copy strided array views, row-major vs. column-major matrices, allocating output arrays with `nb::capsule`, ALTREP considerations, and `nb::data_frame` (`pandas.DataFrame` / `dict` and base R `data.frame` / `tibble`).
5. **[Architecture Deep Dive](architecture.md)** — how `pyrabind` achieves zero Python overhead alongside fast, unwind-safe `.External` R dispatch.
6. **[Overhead & Data-Science Benchmarks](benchmarks.md)** — measured Release `-O3` latency across C++, Python (`pyrabind`, raw `nanobind`, raw CPython C-API, NumPy/pandas), and R (`pyrabind`, raw `.Call`, base R).

---

## Python vs. R Feature Parity Overview

| Capability | Python (`nanobind`) | R (`cpp11` + `.External`) | Notes |
| :--- | :--- | :--- | :--- |
| Free functions & lambdas | Yes | Yes | Identical `m.def(...)` syntax |
| Positional, keyword (`"x"_a`) & default args | Yes | Yes | Two-pass resolution (`convert = false` then `true`) |
| `nb::kw_only()`, `nb::arg().noconvert()`, `.none()` | Yes | Yes | Keyword-only boundary tracked per overload in R |
| `nb::call_guard<Guards...>()` & `gil_scoped_release` / `_acquire` | Yes | Yes | Stack-constructed guards; GIL guards are no-ops in single-threaded R |
| Overloads & `nb::overload_cast` + `nb::const_` | Yes | Yes | Exact integer vs. float types in R (`5L` vs. `5.0`) |
| `nb::class_<T>` & single base `class_<Derived, Base>` | Yes | Yes | Multiple inheritance is Python-only |
| Constructors (`nb::init<...>`) & factories (`nb::new_`) | Yes | Yes | Relinquishable ownership for `unique_ptr` transfer |
| Instance properties (`def_ro`, `def_rw`, `def_prop_ro`, `def_prop_rw`) | Yes (`obj.prop`) | Yes (`obj$prop`) | Pre-indexed hashed R environments |
| Static methods & static properties (`def_static`, `def_*_static`) | Yes (`Cls.prop`) | Yes (`Cls$prop`) | Stored on `.pb_statics` and `.pb_static_props` in R |
| Special methods (`__repr__`, `__len__`, `__getitem__`, `__setitem__`) | Yes | Yes (`print`/`format`, `length`, `[[`, `[[<-`) | `__contains__`: `v in x` (Py) / `` x$`__contains__`(v) `` (R) |
| Arithmetic, bitwise, comparison, unary & in-place operators | Yes | Yes (`Ops` S3 + dunders) | S3 dispatch for `+ - * / %% & \| unary + - ! abs`; dunders for shifts/xor/`~`/in-place/`hash` |
| Iterators (`make_iterator`, `make_key_iterator`, `make_value_iterator`) | Yes (`for` / `list`) | Yes (`as.list` / `$__next__()`) | Raises `StopIteration` / `pyrabind_stop_iteration` when exhausted |
| Opaque containers (`PYRABIND_MAKE_OPAQUE`, `bind_vector`, `bind_map`) | Yes | Yes | Mutated by C++ reference without container copying |
| Enumerations (`nb::enum_<E>`, `.export_values()`) | Yes | Yes (`Enum$Val`, `as.integer`, `==`/`!=`) | Bitwise `Enum \| Enum` is Python-only |
| Lifetime policies (`rv_policy`, `keep_alive<Nurse, Patient>`) | Yes | Yes | Wired through `R_ExternalPtrProtected` in R |
| Built-in & custom exceptions (`nb::exception<CustomError>`) | Python exceptions | R S3 conditions (`CustomError`, `pyrabind_*_error`) | Catchable with `try`/`except` (Py) or `tryCatch` (R) |
| Smart pointers (`std::shared_ptr`, `std::unique_ptr`) | Yes | Yes | `unique_ptr` consumption marks moved-from wrapper invalid |
| STL containers, `std::complex`, `std::chrono`, `std::list` | `<nanobind/stl/*.h>` | All 21 headers | Maps use `std::string` keys in R; `chrono` -> `difftime`/`POSIXct`; `complex` -> `CPLXSXP` |
| Zero-copy `nb::ndarray` views & `capsule` returns | NumPy / buffer / DLPack | R vectors, matrices, arrays (`dim`) | Row-major C++ 2D outputs auto-transpose to column-major R |
| `nb::data_frame` | `pandas.DataFrame` & `dict` | `data.frame` & `tibble` | `float64`, `int64`, `int32`, `bool`, `std::string` columns |
