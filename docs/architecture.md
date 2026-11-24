# Architecture: `nanorbind`

`nanorbind` provides a single C++20 `nanobind`-compatible binding API (`NANORBIND_MODULE`, `nb::class_`, `nb::enum_`, `nb::ndarray`, `nb::data_frame`) that compiles natively into both a **Python 3 extension** (via upstream `nanobind`) and an **R 4 shared library** (via `cpp11` and base R C routines) from one source file with zero target conditionals.

---

## 1. Zero-Overhead Python Backend (`nanobind`)

When `NANORBIND_TARGET_PYTHON` is active:

- [`include/nanorbind/nanorbind.hpp`](../include/nanorbind/nanorbind.hpp) includes upstream `<nanobind/nanobind.h>` directly and re-exports `namespace nanorbind { using namespace ::nanobind; }` and `#define NANORBIND_MODULE(name, var) NB_MODULE(name, var)`.
- `operators.hpp`, `ndarray.hpp`, and `stl/*.hpp` forward straight to upstream `<nanobind/*.h>`.
- **Zero wrapper layer**: There are no adapter objects, extra type tables, heap-allocated argument vectors, or extra C calls around `nanobind`. Compiled binaries and call latency are identical to hand-written `nanobind`.
- [`include/nanorbind/python/dataframe.hpp`](../include/nanorbind/python/dataframe.hpp) provides a native `nanobind::detail::type_caster<nanorbind::data_frame>` supporting `pandas.DataFrame` (including NumPy columns and `pandas` 3.x `'O'` string columns) as well as plain `dict` fallbacks when `numpy` or `pandas` are absent.

---

## 2. High-Performance R Backend (`cpp11` + `.External` + Hashed Environments)

When `NANORBIND_TARGET_R` is active:

- [`include/nanorbind/r/backend.hpp`](../include/nanorbind/r/backend.hpp) implements matching `module_`, `class_<T, Bases...>`, `enum_<E>`, `init<>`, `new_`, `overload_cast`, `rv_policy`, `keep_alive<Nurse, Patient>`, `capsule`, and typed exception factories (`value_error`, `type_error`, `index_error`, `key_error`).
- **`cpp11` Safety Integration** ([`src/r/`](../src/r/)):
  - R headers are always included through `<cpp11/R.hpp>` (`R_NO_REMAP` and `STRICT_R_HEADERS`).
  - Fallible allocating or evaluating R API calls run inside `cpp11::unwind_protect` so R `longjmp` unwinds C++ stack frames safely.
  - Typed C++ exceptions map through an error boundary into structured R S3 conditions (`nanorbind_value_error`, `nanorbind_type_error`, `nanorbind_index_error`, `nanorbind_key_error`) catchable via `tryCatch()`.
- **Low-Overhead `.External` Dispatch & Stack-Allocated Arguments**:
  - Free functions, constructors, and bound methods use R `.External` routines (`nanorbind_ext_call_fn`, `nanorbind_ext_construct`, `nanorbind_ext_call_method`) registered via `R_registerRoutines` inside `R_init_<module>`.
  - Unlike `.Call` wrappers that allocate `list(...)` and `names(args)` on every invocation, `.External` passes R's raw `LISTSXP` call pairlist straight into C++.
  - `call_args` decodes up to 16 positional and keyword arguments onto inline stack arrays (`pos[16]`, `kw[16]`), avoiding `std::vector` heap allocation on typical calls.
- **Pre-Built Per-Class R Environments (`r/nanorbind.R`)**:
  - At module load time (`nanorbind_load`), each C++ class receives a cached `cls_meta` environment containing hashed `methods` and `props` sub-environments chained via `parent.env` to reflect C++ single inheritance.
  - Member lookup (`x$method(...)`, `x$prop`) resolves via a single C `get0(..., inherits = TRUE)` call and passes the resolved method/property external pointer straight to `.External` or `.Call` — eliminating C++ string lookup round-trips on property and method calls.
- **Zero-Copy `ndarray<...>` in R**:
  - R vectors (`REALSXP`, `INTSXP`, `RAWSXP`) and matrices/arrays (`R_DimSymbol`) are wrapped directly without copying using R column-major element strides (`stride(0) = 1`, `stride(1) = nrow`).
  - Returning an owning `nb::ndarray<nb::numpy, T, nb::ndim<2>>(buf, {rows, cols}, owner_capsule)` allocates a native R matrix and copies row-major C++ buffers into column-major R layout cleanly before invoking the capsule cleanup.
