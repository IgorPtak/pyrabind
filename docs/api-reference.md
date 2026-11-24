# API Reference

Every `pyrabind` binding translation unit includes `pyrabind/pyrabind.hpp` (`include/pyrabind/pyrabind.hpp`, plus optional feature headers) and aliases `namespace nb = pyrabind;`. On Python builds, these declarations forward straight to `::nanobind`. On R builds, they bind cleanly to the `cpp11` runtime.

---

## 1. Headers

| Header | Contents |
| :--- | :--- |
| `#include "pyrabind/pyrabind.hpp"` | `PYRABIND_MODULE`, `PYRABIND_MAKE_OPAQUE`, `module_`, `class_`, `enum_`, `exception<T>`, `handle`, `type<T>()`, `arg`, `literals` (`"_a"`), `init`, `new_`, `overload_cast`, `const_`, `kw_only`, `call_guard`, `gil_scoped_acquire`/`_release`, `rv_policy`, `keep_alive`, `capsule`, `none()`, built-in exception factories, `std::string`, `std::string_view` |
| `#include "pyrabind/make_iterator.hpp"` | `make_iterator`, `make_key_iterator`, `make_value_iterator` |
| `#include "pyrabind/ndarray.hpp"` | `ndarray`, `ndim<N>`, `shape<...>`, `c_contig`, `f_contig`, `any_contig`, `numpy`, `pytorch`, `no_framework`, `ro`, `device::cpu` |
| `#include "pyrabind/dataframe.hpp"` | `data_frame`, `column_data`, and host casters for `pandas.DataFrame` / `dict` (Python) and `data.frame` / `tibble` (R) |
| `#include "pyrabind/dataframe_types.hpp"` | Backend-free `pyrabind::column_data` and `pyrabind::data_frame` definitions (usable with `PYRABIND_CORE_ONLY`) |
| `#include "pyrabind/operators.hpp"` | `nb::self`, `nb::abs`, `nb::hash`, and operator expression templates |
| `#include "pyrabind/stl.hpp"` | Convenience header including all 19 STL/smart-pointer/chrono/complex/callback casters (`pyrabind/stl/*.hpp`) |
| `#include "pyrabind/stl/bind_vector.hpp"` | Opt-in opaque binding helper `nb::bind_vector<Vector>(scope, name, ...)` |
| `#include "pyrabind/stl/bind_map.hpp"` | Opt-in opaque binding helper `nb::bind_map<Map>(scope, name, ...)` |

---

## 2. Modules, Functions, Attributes & Submodules

### Module Entry Point

```cpp
#include "pyrabind/pyrabind.hpp"

namespace nb = pyrabind;

PYRABIND_MODULE(my_module, m) {
    m.doc() = "Module docstring";
    m.attr("VERSION") = "1.0.0";
    m.attr("PI") = 3.141592653589793;
}
```

- `PYRABIND_MODULE(name, var)` expands to `NB_MODULE(name, var)` on Python and defines `R_init_<name>(DllInfo*)` on R. Keep `<name>` aligned with the first argument passed to `pyrabind_add_module(<name> ...)`.
- `m.doc() = "..."` attaches `__doc__` on Python and stores the module docstring in the C++ R `module_record`.
- `m.attr("NAME") = value` binds module-level constants into the Python module and into the R module environment.

### Free Functions & Lambdas

```cpp
int add_defaults(int a, int b = 10, int c = 100) { return a + b + c; }

PYRABIND_MODULE(my_module, m) {
    using namespace nb::literals;

    m.def("add_defaults", &add_defaults,
          "a"_a, "b"_a = 10, "c"_a = 100,
          "Add three integers");

    m.def("square", [](double x) { return x * x; }, "x"_a);
}
```

Supported extra annotations passed to `.def(...)`:

- **`"name"_a`** or **`nb::arg("name")`**: Names positional/keyword parameter.
- **`"name"_a = default_value`**: Supplies a default value (`nb::none()` / `nullptr` maps to Python `None` and R `NULL`).
- **`.noconvert()`**: Disallows implicit conversion (Pass 2 coercion) for this argument, e.g., `"x"_a.noconvert()`.
- **`.none()`**: Allows `None` / `NULL` for pointer arguments (`T*` receives `nullptr`).
- **`"docstring"`**: Plain C-string (`const char*`) stored as documentation metadata.
- **Overload & tag markers**: `nb::rv_policy`, `nb::keep_alive<Nurse, Patient>`, `nb::kw_only()`, `nb::call_guard<Guards...>()` (including `nb::gil_scoped_release` / `nb::gil_scoped_acquire`), `nb::is_method()`, `nb::is_operator()`, `nb::is_implicit()`, `nb::is_arithmetic()`.

### Keyword-Only Arguments & Call Guards

```cpp
m.def("kw_combine", [](int a, int b, int c) { return a * 100 + b * 10 + c; },
      "a"_a, nb::kw_only(), "b"_a, "c"_a = 5);

m.def("guarded_work", &work, "x"_a,
      nb::call_guard<nb::gil_scoped_release, CustomGuard>());
```

- Any argument after `nb::kw_only()` must be passed by keyword (`kw_combine(1, b=2)` works; `kw_combine(1, 2)` fails overload matching on both Python and R).
- `nb::call_guard<Guards...>()` constructs each guard on entry and destroys it on exit; `nb::gil_scoped_release` and `nb::gil_scoped_acquire` compile cleanly as zero-cost no-ops on the single-threaded R target while releasing/acquiring the GIL on Python.
- If a function allocates and returns a new `nb::capsule` (e.g., owning an output `nb::ndarray`), do **not** attach `nb::call_guard<nb::gil_scoped_release>()` to the whole wrapper, because `nb::capsule` calls `PyCapsule_New` before the call guard destructor restores the GIL. Wrap only the pure C++ compute loop in a scoped `{ nb::gil_scoped_release nogil; ... }` block before constructing `nb::capsule`.

### Two-Pass Overload Resolution

Multiple `.def("name", ...)` calls under the same name register overloads resolved identically on Python and R:

1. **Pass 1 (strict, `convert = false`)**: Tries every registered overload without implicit numeric/container coercion (e.g., R `5L` / `INTSXP` matches `int` before `double`, while `5.0` / `REALSXP` matches `double`).
2. **Pass 2 (converting, `convert = true`)**: If Pass 1 finds no match, retries overloads allowing safe conversions (e.g., `int` -> `double`, integer/real -> `bool`, `enum` from integer, single `list`/`c()` element conversions) for parameters not marked `.noconvert()`.

### Disambiguating Overloaded C++ Functions (`nb::overload_cast`)

```cpp
std::string classify(int x);
std::string classify(double x);

struct Accumulator {
    int add(int x);
    int add(int x) const;
};

m.def("classify", nb::overload_cast<int>(&classify), "x"_a);
m.def("classify", nb::overload_cast<double>(&classify), "x"_a);

nb::class_<Accumulator>(m, "Accumulator")
    .def("add", nb::overload_cast<int>(&Accumulator::add), "x"_a)
    .def("add_const", nb::overload_cast<int>(&Accumulator::add, nb::const_), "x"_a);
```

### Submodules

```cpp
auto math = m.def_submodule("math", "Math helper functions");
math.def("square", [](double x) { return x * x; }, "x"_a);
```

Accessible as `mod.math.square(4.5)` in Python and `mod$math$square(4.5)` in R.

---

## 3. Classes, Constructors, Properties & Inheritance

### Binding a Class & Single Inheritance

```cpp
struct Animal {
    std::string name;
    explicit Animal(std::string n) : name(std::move(n)) {}
    virtual ~Animal() = default;
    virtual std::string speak() const { return name + ": ..."; }
};

struct Dog : Animal {
    using Animal::Animal;
    std::string speak() const override { return name + ": woof"; }
    std::string fetch() const { return name + " fetched the ball"; }
};

nb::class_<Animal>(m, "Animal")
    .def(nb::init<std::string>(), "name"_a)
    .def_ro("name", &Animal::name)
    .def("speak", &Animal::speak);

nb::class_<Dog, Animal>(m, "Dog")
    .def(nb::init<std::string>(), "name"_a)
    .def("fetch", &Dog::fetch);
```

- **Inheritance**: `nb::class_<Derived, Base>` supports **single inheritance** on both Python and R. R automatically chains `methods` and `props` hashed environments via `parent.env` and emits ordered S3 class vectors `c("mod.Dog", "mod.Animal", "pyrabind_object")`, allowing polymorphic calls (`greet_animal(dog)`) and upcasts to `Base&` / `const Base*`.
- **Constructors & Factories**:
  - `.def(nb::init<Args...>())` binds a C++ constructor.
  - `.def(nb::new_(&factory_fn))` binds a factory callable returning an instance by value or pointer.
- **Methods, Static Methods & Type Handles**:
  - `.def("name", &Class::method, ...)` binds member functions or free/lambda adapters taking `Class&` / `const Class&` as the first parameter.
  - `.def_static("name", &fn, ...)` binds static methods callable as `Class.name(...)` in Python and `Class$name(...)` in R.
  - `nb::handle` and `nb::type<T>()` provide generic handle interoperability (including `module_::operator handle()` and static property callbacks).
- **Instance & Static Fields / Properties**:
  - `.def_ro("x", &Class::x)` and `.def_rw("x", &Class::x)` expose public C++ fields (also inherited from base class members).
  - `.def_prop_ro("norm", &Class::norm)` exposes a read-only getter property (`obj.norm` / `obj$norm`).
  - `.def_prop_rw("val", &Class::get_val, &Class::set_val)` exposes a read/write property (`obj.val = v` / `obj$val <- v`).
  - `.def_ro_static("CONST", &Class::kConst)` and `.def_rw_static("mut", &Class::mut)` expose static C++ variables directly on the bound class (`Cls.CONST` / `Cls$CONST`).
  - `.def_prop_ro_static("calc", [](nb::handle) { ... })` and `.def_prop_rw_static("prop", [](nb::handle) { ... }, [](nb::handle, T v) { ... })` take `nb::handle` as the first parameter for upstream `nanobind` compatibility and work identically on Python (`Cls.prop = v`) and R (`Cls$prop <- v`).

---

## 4. Special Methods, Iterators, Containers & Operators

### Special Dunder Methods

| C++ Binding | Python Usage | R Usage | Notes |
| :--- | :--- | :--- | :--- |
| `.def("__repr__", &T::repr)` | `repr(obj)` / `str(obj)` | `print(obj)` / `format(obj)` | Falls back to `<pyrabind qualname>` in R if omitted |
| `.def("__len__", &T::size)` | `len(obj)` | `length(obj)` | Returns `1L` in R if `__len__` is not bound |
| `.def("__getitem__", &T::get)` | `obj[idx]` | `obj[[idx]]` | Key/index is forwarded unchanged (0-based C++ unless adjusted) |
| `.def("__setitem__", &T::set)` | `obj[idx] = val` | `obj[[idx]] <- val` | Mutates C++ object in place via external pointer |
| `.def("__contains__", &T::contains)` | `val in obj` | `` obj$`__contains__`(val) `` | Base R does not dispatch `%in%` over S3 objects |
| `.def("__iter__", ...)` | `iter(obj)` / `for x in obj` | `` obj$`__iter__`() `` / `as.list(it)` | Returns a `pyrabind_iterator` in R supporting `$__next__()` |

### Iterators (`pyrabind/make_iterator.hpp`)

```cpp
#include "pyrabind/make_iterator.hpp"

nb::class_<NumberBag>(m, "NumberBag")
    .def("__iter__", [](const NumberBag& b) {
        return nb::make_iterator(nb::type<NumberBag>(), "ValIter", b.vals);
    }, nb::keep_alive<0, 1>())
    .def("keys", [](const NumberBag& b) {
        return nb::make_key_iterator(nb::type<NumberBag>(), "KeyIter", b.kv.begin(), b.kv.end());
    }, nb::keep_alive<0, 1>())
    .def("values", [](const NumberBag& b) {
        return nb::make_value_iterator(nb::type<NumberBag>(), "MapValIter", b.kv.begin(), b.kv.end());
    }, nb::keep_alive<0, 1>())
    .def("items", [](const NumberBag& b) {
        return nb::make_iterator(nb::type<NumberBag>(), "ItemIter", b.kv.begin(), b.kv.end());
    }, nb::keep_alive<0, 1>());
```

In Python, consume directly with `list(bag)` / `for`. In R, materialize via `as.list(bag$keys())` or step manually with `` it <- bag$keys(); it$`__next__`() `` until `pyrabind_stop_iteration` is raised.

### Opaque Containers (`PYRABIND_MAKE_OPAQUE`, `bind_vector`, `bind_map`)

Declare `PYRABIND_MAKE_OPAQUE(VecType)` / `PYRABIND_MAKE_OPAQUE(MapType)` at global scope after `#include "pyrabind/stl.hpp"` and before binding functions, then bind container methods directly with zero copy-on-call overhead:

```cpp
#include "pyrabind/pyrabind.hpp"
#include "pyrabind/stl.hpp"
#include "pyrabind/stl/bind_vector.hpp"
#include "pyrabind/stl/bind_map.hpp"

using OpaqueVec = std::vector<Item>;
using OpaqueMap = std::map<std::string, Item>;

PYRABIND_MAKE_OPAQUE(OpaqueVec)
PYRABIND_MAKE_OPAQUE(OpaqueMap)

PYRABIND_MODULE(mod, m) {
    nb::bind_vector<OpaqueVec>(m, "OpaqueVec");
    nb::bind_map<OpaqueMap>(m, "OpaqueMap");
}
```

### Operators (`pyrabind/operators.hpp`)

```cpp
#include "pyrabind/operators.hpp"

nb::class_<Vec2>(m, "Vec2")
    .def(-nb::self)
    .def(nb::self + nb::self)
    .def(nb::self - nb::self)
    .def(nb::self * double())
    .def(double() * nb::self)
    .def(nb::self / double())
    .def(nb::self == nb::self)
    .def(nb::self != nb::self)
    .def(nb::self < nb::self)
    .def(nb::self <= nb::self)
    .def(nb::self > nb::self)
    .def(nb::self >= nb::self);
```

Supported dual-target operators in `pyrabind/operators.hpp`:
- **Unary**: `-nb::self` (`__neg__`), `+nb::self` (`__pos__`), `~nb::self` (`__invert__`), `!nb::self` (`__bool__`), `nb::abs(nb::self)` (`__abs__`), `nb::hash(nb::self)` (`__hash__`)
- **Binary** (`nb::self op T`, `T op nb::self`, `nb::self op nb::self`): `+`, `-`, `*`, `/` (truediv), `%`, `<<`, `>>`, `&`, `^`, `|`
- **In-place**: `+=`, `-=`, `*=`, `/=`, `%=`, `<<=`, `>>=`, `&=`, `^=`, `|=`
- **Comparisons**: `==`, `!=`, `<`, `<=`, `>`, `>=`

In R, `Ops.pyrabind_object` and `abs.pyrabind_object` dispatch unary `+`, `-`, `!`, `abs(x)` and binary `+`, `-`, `*`, `/`, `%%`, `&`, `|`, `==`, `!=`, `<`, `<=`, `>`, `>=` with right-hand (`__radd__`, etc.) and flipped comparison symmetry; bitwise shifts, `^`, `~`, in-place (`__iadd__`, etc.), and `__hash__` are called directly via `` obj$`__iadd__`(other) ``.

---

## 5. Enumerations (`nb::enum_<E>`)

```cpp
enum class Color : std::int32_t { Red = 1, Green = 2, Blue = 4 };

nb::enum_<Color>(m, "Color", nb::is_arithmetic())
    .value("Red", Color::Red, "Red channel")
    .value("Green", Color::Green)
    .value("Blue", Color::Blue)
    .export_values();
```

- **Python**: `mod.Color.Green`, `int(mod.Color.Green) == 2`, `mod.Color.Green.value == 2`, `mod.Red` (via `export_values()`), `==`/`!=`, arithmetic via `nb::is_arithmetic()`.
- **R**: `mod$Color$Green`, `as.integer(mod$Color$Green) == 2L`, `mod$Color$Green$value`, `mod$Color$Green$name == "Green"`, `mod$Red` (via `export_values()`), `==`/`!=`, `print(e)` (`Color.Green`).

---

## 6. Ownership, Lifetimes & Capsules

### Return Value Policies (`nb::rv_policy`)

Properties default to `nb::rv_policy::reference_internal`; functions and methods default to `nb::rv_policy::automatic`.

| Policy | Behavior |
| :--- | :--- |
| `automatic` / `automatic_reference` | Moves/copies values; takes ownership of newly returned pointers, or references lvalue references tied to parent (`automatic_reference`) |
| `take_ownership` | Wraps returned C++ pointer and deletes it when Python/R reclaims the handle |
| `copy` | Copy-constructs an owned host wrapper |
| `move` | Move-constructs an owned host wrapper |
| `reference` | Non-owning reference/pointer; caller must ensure C++ lifetime |
| `reference_internal` | Ties returned child object lifetime to parent (`nurse` protects `patient` via `R_ExternalPtrProtected` in R) |
| `none` | Refuses returning unmanaged C++ type |

Example:
```cpp
.def("get_embedded", &Container::get_embedded, nb::rv_policy::reference_internal)
```

### `nb::keep_alive<Nurse, Patient>`

Keeps `Patient` alive at least as long as `Nurse` (`0` = return value, `1` = `self` for methods or 1st arg for functions, `2` = 1st explicit method argument, etc.):

```cpp
.def("attach", &Container::attach, nb::keep_alive<1, 2>())
```

In R, deleting `res` and forcing `gc()` after `box$attach(res)` leaves `res` protected in `R_ExternalPtrProtected(box)` until `box` itself is garbage-collected.

### `nb::capsule`

Manages opaque `void*` allocations with a `void (*)(void*) noexcept` destructor, primarily used to hand ownership of heap arrays cleanly to `nb::ndarray`:

```cpp
auto* buf = new double[n];
nb::capsule owner(buf, [](void* p) noexcept { delete[] static_cast<double*>(p); });
return nb::ndarray<nb::numpy, double, nb::ndim<1>>(buf, {n}, owner);
```

---

## 7. Exceptions & Error Conditions

Throw `nb::value_error(...)`, `nb::type_error(...)`, `nb::index_error(...)`, `nb::key_error(...)`, `nb::attribute_error(...)`, `nb::stop_iteration(...)`, or standard `std::exception` subclasses cleanly from C++:

| C++ Exception | Python Exception | R S3 Condition Class Hierarchy |
| :--- | :--- | :--- |
| `nb::value_error` / `std::invalid_argument` | `ValueError` | `c("pyrabind_value_error", "pyrabind_error", "error", "condition")` |
| `nb::type_error` | `TypeError` | `c("pyrabind_type_error", "pyrabind_error", "error", "condition")` |
| `nb::index_error` / `std::out_of_range` | `IndexError` | `c("pyrabind_index_error", "pyrabind_error", "error", "condition")` |
| `nb::key_error` | `KeyError` | `c("pyrabind_key_error", "pyrabind_error", "error", "condition")` |
| `nb::attribute_error` / missing R member | `AttributeError` | `c("pyrabind_attribute_error", "pyrabind_error", "error", "condition")` |
| `nb::stop_iteration` | `StopIteration` | `c("pyrabind_stop_iteration", "pyrabind_error", "error", "condition")` |
| `std::runtime_error` / `std::exception` | `RuntimeError` | `c("pyrabind_runtime_error", "pyrabind_error", "error", "condition")` |

Catching in R with `tryCatch`:

```r
tryCatch(
    tm$check_positive(-1L),
    pyrabind_value_error = function(e) message("Caught value error: ", e$message)
)
```

### Custom Exceptions (`nb::exception<T>`)

Register domain-specific C++ exceptions (deriving from `std::exception`) with `nb::exception<T>(m, "Name")`:

```cpp
struct CustomDomainError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

PYRABIND_MODULE(mod, m) {
    nb::exception<CustomDomainError>(m, "CustomDomainError");
    m.def("fail_domain", [](const std::string& msg) -> int {
        throw CustomDomainError(msg);
    });
}
```

- **Python**: Raises `mod.CustomDomainError` (subclass of `Exception`), catchable via `except mod.CustomDomainError:`.
- **R**: Emits an S3 condition hierarchy starting with `CustomDomainError` (and `pyrabind_runtime_error` / `error` / `condition`), catchable directly via `tryCatch(..., CustomDomainError = function(e) ...)`.

---

## 8. Stub & R Wrapper Generation (`tools/pyrabind_stubgen.py`)

`tools/pyrabind_stubgen.py` reflects over a compiled Python module (`nanobind` docstrings, classes, properties, enums, and functions) to emit:

- **PEP 484 type stubs (`.pyi`)** for IDEs, `mypy`, and `pyright`, optionally AST-validated via `--verify`.
- **`roxygen2`-documented R wrappers (`.R`)** exporting user-friendly top-level R functions delegating to the loaded `pyrabind` environment.

See [Getting Started & Build Guide](getting-started.md) for CLI invocation examples.
