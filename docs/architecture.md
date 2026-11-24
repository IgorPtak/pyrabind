# Architecture: `pyrabind`

`pyrabind` provides a single C++20 `nanobind`-compatible binding API (`PYRABIND_MODULE`, `nb::class_`, `nb::enum_`, `nb::ndarray`, `nb::data_frame`) that compiles natively into both a **Python 3 extension** (via upstream `nanobind`) and an **R 4 shared library** (via `cpp11` and base R C routines) from one source file with zero target conditionals.

```mermaid
flowchart LR
    Src["Single C++20 Module (PYRABIND_MODULE)"]
    Src -->|"PYRABIND_TARGET_PYTHON"| PyHdr["pyrabind/pyrabind.hpp -> nanobind"]
    Src -->|"PYRABIND_TARGET_R"| RHdr["pyrabind/r/backend.hpp -> cpp11 + .External"]
    PyHdr --> PySO["<mod>.cpython-*.so (Zero Wrapper Layer)"]
    RHdr --> RSO["<mod>.so + r/pyrabind.R (Base R S3 Env)"]
```

---

## 1. Zero-Overhead Python Backend (`nanobind`)

When `PYRABIND_TARGET_PYTHON` is active:

- [`include/pyrabind/pyrabind.hpp`](../include/pyrabind/pyrabind.hpp) includes upstream `<nanobind/nanobind.h>` directly and re-exports `namespace pyrabind { using namespace ::nanobind; }` and `#define PYRABIND_MODULE(name, variable) NB_MODULE(name, var)`.
- `operators.hpp`, `ndarray.hpp`, and `stl/*.hpp` forward straight to upstream `<nanobind/*.h>`.
- **Zero wrapper layer**: There are no adapter objects, extra type tables, heap-allocated argument vectors, or extra C calls around `nanobind`. Compiled binaries and call latency are identical to hand-written `nanobind`.
- [`include/pyrabind/python/dataframe.hpp`](../include/pyrabind/python/dataframe.hpp) provides a native `nanobind::detail::type_caster<pyrabind::data_frame>` supporting `pandas.DataFrame` (including NumPy columns and `pandas` 3.x `'O'` string columns) as well as plain `dict` fallbacks when `numpy` or `pandas` are absent.

---

## 2. High-Performance R Backend (`cpp11` + `.External` + Hashed Environments)

When `PYRABIND_TARGET_R` is active:

- [`include/pyrabind/r/backend.hpp`](../include/pyrabind/r/backend.hpp) implements matching `module_`, `class_<T, Bases...>`, `enum_<E>`, `init<>`, `new_`, `overload_cast`, `rv_policy`, `keep_alive<Nurse, Patient>`, `capsule`, and typed exception factories (`value_error`, `type_error`, `index_error`, `key_error`, `attribute_error`, `stop_iteration`).
- **`cpp11` Safety Integration** ([`src/r/`](../src/r/)):
  - R headers are always included through `<cpp11/R.hpp>` (`R_NO_REMAP` and `STRICT_R_HEADERS`).
  - Fallible allocating or evaluating R API calls run inside `cpp11::unwind_protect` / `cpp11::safe[...]` so R `longjmp` unwinds C++ stack frames cleanly.
  - `r_error_boundary` catches every C++ exception and translates typed exceptions into structured R S3 conditions (`pyrabind_value_error`, `pyrabind_type_error`, `pyrabind_index_error`, `pyrabind_key_error`, `pyrabind_attribute_error`, `pyrabind_stop_iteration`, `pyrabind_runtime_error`) catchable via `tryCatch()`.
- **Low-Overhead `.External` Dispatch & Stack-Allocated Arguments**:
  - Free functions, constructors, and bound methods use R `.External` routines (`pyrabind_ext_call_fn`, `pyrabind_ext_construct`, `pyrabind_ext_call_method`) registered via `R_registerRoutines` with `R_useDynamicSymbols(dll, FALSE)` inside `R_init_<module>`.
  - Unlike `.Call` wrappers that allocate `list(...)` and `names(args)` on every invocation, `.External` passes R's raw `LISTSXP` call pairlist straight into C++.
  - `call_args` decodes up to 16 positional and keyword arguments onto inline stack arrays (`inline_pos[16]`, `inline_kw_names[16]`, `inline_kw_vals[16]`), allocating on the heap only when more than 16 arguments are passed.
- **Pre-Built Per-Class R Environments ([`r/pyrabind.R`](../r/pyrabind.R))**:
  - At module load time (`pyrabind_load`), each C++ class receives a cached `cls_meta` environment containing hashed `methods` and `props` sub-environments chained via `parent.env` to mirror C++ single inheritance.
  - Member lookup (`x$method(...)`, `x$prop`) resolves via a single C `get0(..., inherits = TRUE)` call in base R and passes the resolved method/property external pointer directly to `.External` or `.Call` — eliminating C++ string lookup round-trips on property and method invocations.
- **Lifetime Management & Zero-Copy `ndarray`**:
  - `rv_policy::reference_internal`, `automatic_reference`, and `nb::keep_alive<Nurse, Patient>` chain `patient` SEXP references onto `R_ExternalPtrProtected(nurse)` so the R garbage collector keeps linked objects alive as long as the nurse handle exists.
  - R vectors (`REALSXP`, `INTSXP`, `RAWSXP`) and matrices/arrays (`R_DimSymbol`) wrap directly without copying using R column-major strides (`stride(0) = 1`, `stride(1) = nrow`), while owning `nb::ndarray` outputs materialize native column-major R vectors/matrices and release the C++ `capsule`.
