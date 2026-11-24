# Type Conversions & Python/R Semantics

`pyrabind` maps C++ fundamental types, strings, STL containers, and callbacks to idiomatic **Python** objects (via upstream `nanobind`) and **R** objects (via `cpp11` and base R SEXP casters).

---

## 1. Scalars & Strings

`std::string` and `std::string_view` are enabled automatically by `#include "pyrabind/pyrabind.hpp"`.

| C++ Type | Python Representation | R Representation | Notes |
| :--- | :--- | :--- | :--- |
| `bool` | `bool` | `logical(1)` (`LGLSXP`) | R `NA` fails conversion (`false`); in Pass 2 (`convert=true`), R `1L`/`0L` and finite `1.0`/`0.0` coerce to `bool` |
| Signed integers (`int`, `int32_t`, `int64_t`) | `int` | `integer(1)` (`INTSXP`) or `numeric(1)` (`REALSXP`) | R inputs prefer `INTSXP` (`5L`) in Pass 1; outputs in `(NA_INTEGER, 2147483647]` return `integer(1)`, larger magnitudes return `double(1)` (exact up to `2^53`) |
| Unsigned integers (`uint32_t`, `uint64_t`) | `int` | `integer(1)` or `numeric(1)` | Rejects negative inputs; outputs `<= 2147483647` return `integer(1)`, larger return `double(1)` |
| `float`, `double` | `float` | `numeric(1)` (`REALSXP`) | In Pass 2 (`convert=true`), R `integer`/`logical` scalars promote to `double` |
| `std::string`, `std::string_view`, `const char*` | `str` (UTF-8) | `character(1)` (`STRSXP`, UTF-8) | R fast-path copies pure ASCII/UTF-8 bytes directly without `unwind_protect`; `NA_character_` (`NA_STRING`) fails conversion; `const char*` maps `None`/`NULL` <-> `nullptr` |
| `pyrabind::none_t` / `std::nullptr_t` | `None` | `NULL` (`R_NilValue`) | Also produced by `nb::none()` defaults |

---

## 2. STL Container, Smart Pointer & Utility Headers

Include `#include "pyrabind/stl.hpp"` to pull in all 19 caster headers below, or include individual headers under `pyrabind/stl/*.hpp` (plus the 2 opt-in opaque binding helpers `bind_vector.hpp` and `bind_map.hpp`):

| Header | C++ Types | Python Host Type | R Host Type |
| :--- | :--- | :--- | :--- |
| `stl/string.hpp` | `std::string` | `str` | `character(1)` (built-in) |
| `stl/string_view.hpp` | `std::string_view` | `str` | `character(1)` (built-in) |
| `stl/complex.hpp` | `std::complex<T>` | `complex` | `complex(1)` (`CPLXSXP`, e.g., `1 + 2i`) |
| `stl/chrono.hpp` | `std::chrono::duration`, `time_point` | `datetime.timedelta` / `datetime.datetime` | `difftime` (`"secs"`) / `POSIXct` (`"UTC"`) or `Date` |
| `stl/vector.hpp` | `std::vector<T>` | `list` | Homogeneous **atomic vector** (`REALSXP`, `INTSXP`, `LGLSXP`, `STRSXP`), or `list` (`VECSXP`) for nested/object elements; memcpy fast-path for `vector<double>` |
| `stl/list.hpp` | `std::list<T, Alloc>` | `list` | Homogeneous atomic vector or `list` |
| `stl/array.hpp` | `std::array<T, N>` | `list` (exact length `N`) | Atomic vector / `list` of exact length `N` |
| `stl/pair.hpp` | `std::pair<T1, T2>` | 2-tuple | 2-element R `list` (`VECSXP`) |
| `stl/tuple.hpp` | `std::tuple<Ts...>` | `tuple` | Fixed-length R `list` (`VECSXP`) |
| `stl/optional.hpp` | `std::optional<T>` | `T` or `None` | `T` or `NULL` |
| `stl/variant.hpp` | `std::variant<Ts...>` | First matching alternative | Two-pass resolution across alternatives (Pass 1 strict, then Pass 2 converting) |
| `stl/set.hpp` | `std::set<T>` | `set` | Deduplicated vector / `list` |
| `stl/unordered_set.hpp` | `std::unordered_set<T>` | `set` | Deduplicated vector / `list` |
| `stl/map.hpp` | `std::map<K, V>` | `dict` | Named R `list` (`K` must be `std::string` on R) |
| `stl/unordered_map.hpp` | `std::unordered_map<K, V>` | `dict` | Named R `list` (`K` must be `std::string` on R) |
| `stl/shared_ptr.hpp` | `std::shared_ptr<T>` | Shared wrapper (`PyObject` / control block) | Shared `instance_box` holder refcount (`sp` mutations visible from R & C++) |
| `stl/unique_ptr.hpp` | `std::unique_ptr<T>` | Ownership transfer + moved-from trap | `r_relinquish_unique_ptr` clears wrapper pointer; reused wrapper raises error |
| `stl/filesystem.hpp` | `std::filesystem::path` | `pathlib.Path` / `str` | `character(1)` path string |
| `stl/function.hpp` | `std::function<R(Args...)>` | Python callable / C++ fn wrapper | R `function` (`CLOSXP` / `BUILTINSXP`) / `pyrabind_function` |
| `stl/bind_vector.hpp` | `nb::bind_vector<Vector>(...)` | Native sequence class | `pyrabind_object` container class (`append`, `[[`, `length`, `__iter__`, etc.) |
| `stl/bind_map.hpp` | `nb::bind_map<Map>(...)` | Native mapping class | `pyrabind_object` map class (`keys`, `values`, `items`, `[[`, `[[<-`, etc.) |

To avoid copying bound containers passed by reference (`Vec&` / `Map&`) when using `bind_vector` / `bind_map`, mark them with `PYRABIND_MAKE_OPAQUE(Type)` at global scope after `#include "pyrabind/stl.hpp"`.

---

## 3. Bidirectional Callbacks (`pyrabind/stl/function.hpp`)

C++ functions can both **accept** host lambdas/functions and **return** C++ closures:

```cpp
#include "pyrabind/stl/function.hpp"
#include "pyrabind/stl/vector.hpp"

std::vector<double> map_doubles(const std::vector<double>& xs,
                                const std::function<double(double)>& fn);
std::function<int(int)> make_adder(int k);
```

- **Host -> C++**: Passing `lambda v: v * 10.0` (Python) or `function(v) v * 10.0` (R) wraps the callable safely (protected by a `cpp11::sexp` lease in R and evaluated through `cpp11::unwind_protect`). Passing `None`/`NULL` converts to an empty `std::function{}`.
- **C++ -> Host**: Returning a `std::function<int(int)>` wraps it in a callable `nanobind` object on Python or a `pyrabind_function` closure backed by `.External` on R.

---

## 4. Python vs. R Behavioral Differences & Best Practices

Even with one shared C++ source file, Python and R have different host-language conventions:

1. **Overload matching on numeric literals in R**:
   - In R, bare numeric literals (`7`) have type `double` (`REALSXP`), whereas `7L` has type `integer` (`INTSXP`).
   - Because Pass 1 skips implicit widening/narrowing, calling an overloaded function `classify(int)` vs `classify(double)` from R dispatches `tm$classify(7L)` to `int` and `tm$classify(7.5)` or `tm$classify(7)` to `double`.
2. **0-based C++ indices in `__getitem__` / `__setitem__`**:
   - R's `obj[[i]]` forwards `i` directly to your bound C++ `__getitem__` method (`std::ptrdiff_t` / `std::size_t`), so C++-style `0`-based and negative indices behave identically to Python (`box[[0L]]`, `box[[-1L]] <- 77L`) unless your C++ method adapts indices itself.
3. **Associative maps in R require `std::string` keys**:
   - R lists only have string names (`R_NamesSymbol`), so `std::map<std::string, V>` and `std::unordered_map<std::string, V>` round-trip cleanly as R named lists (`list(a = 1L)`), whereas non-string map keys (e.g. `std::map<int, V>`) are Python-only.
4. **Membership (`__contains__`) in R**:
   - Python compiles `x in obj` to `__contains__`. Base R has no S3 generic hook on `%in%`, so call `` obj$`__contains__`(val) `` when testing membership from R.
5. **Caching R `$` lookups in hot loops**:
   - Calling `obj$method(...)` inside a tight R loop performs one `$` method environment lookup per iteration (`~2.36 us` on `BenchPoint.dot`). Extracting the bound closure once before the loop (`pdot <- obj$dot; pdot(other)`) drops R call latency to **`487.2 ns`**.
6. **Single inheritance limit in R**:
   - `nb::class_<Derived, Base>` supports one base class in `pyrabind/r/backend.hpp` (enforced by `static_assert(sizeof...(Bases) <= 1)`).
7. **Interactive tab completion in R**:
   - `r/pyrabind.R` registers `.DollarNames.pyrabind_object`, giving RStudio / R console tab completion across all properties and methods including inherited base-class members.
