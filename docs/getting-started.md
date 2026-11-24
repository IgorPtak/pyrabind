# Getting Started & Build Guide

This guide covers toolchain requirements, CMake presets and options, building external extensions against an installed `pyrabind`, and loading compiled modules in Python and R.

---

## 1. Prerequisites

| Dependency | Version Tested | Required For | Notes |
| :--- | :--- | :--- | :--- |
| **C++ compiler** | AppleClang 21 / GCC / Clang / MSVC | All targets | Must support **C++20** (`-std=c++20`) |
| **CMake** | `>= 3.20` | All targets | `Ninja` recommended (used by `CMakePresets.json`) |
| **Python 3** | `3.12.8` | Python backend (`PYRABIND_BUILD_PYTHON=ON`) | Interpreter + development module headers |
| **`nanobind`** (Python package) | `3.1.0` | Python backend (`PYRABIND_BUILD_PYTHON=ON`) | Discovered via `python3 -m nanobind --cmake_dir` |
| **NumPy** & **pandas** (optional) | `2.4.4` / `3.0.2` | Python tests, benchmarks & DataFrames | Without pandas, `pyrabind::data_frame` falls back to `dict` |
| **R** | `4.5.3` | R backend (`PYRABIND_BUILD_R=ON`) | `Rscript`, R headers, `libR`, base R only at runtime |
| **`cpp11`** (R package) | `0.5.2` | R backend (`PYRABIND_BUILD_R=ON`) | Header-only compile-time dependency; discovered via `FindCpp11.cmake` |

> [!NOTE]
> `r/pyrabind.R` and compiled R modules use **pure base R** at runtime — end users loading your compiled R extension do not need `cpp11` installed unless they recompile from C++ source.

---

## 2. Building & Testing the Repository

### Using CMake Presets

Two Ninja presets are configured in `CMakePresets.json`:

```bash
# Release build (-O3 -DNDEBUG)
cmake --preset release
cmake --build build-release -j
ctest --test-dir build-release --output-on-failure

# Default debug/unoptimized development build
cmake --preset default
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### Selecting Custom Python or `cpp11` Locations

If your active `python3` or `cpp11` package lives in a custom environment, pass the variables explicitly at configure time:

```bash
cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPython_EXECUTABLE=/path/to/venv/bin/python3 \
  -DCPP11_INCLUDE_DIR=/path/to/R/library/cpp11/include
cmake --build build-release -j
```

*(Note: if `Python3_EXECUTABLE` is passed without `Python_EXECUTABLE`, `CMakeLists.txt` automatically forwards `Python3_EXECUTABLE` to `Python_EXECUTABLE`.)*

### CMake Configuration Options

| Option | Default | Effect |
| :--- | :--- | :--- |
| `PYRABIND_BUILD_PYTHON` | `ON` | Enables Python target discovery (`nanobind`), `pyrabind::python`, and Python modules |
| `PYRABIND_BUILD_R` | `ON` | Enables R & `cpp11` discovery, `pyrabind_r_runtime`, `pyrabind::r`, and R modules |
| `PYRABIND_BUILD_TESTS` | `ON` | Builds and registers C++, Python, R, and `geometry` CTest targets |
| `PYRABIND_BUILD_EXAMPLES` | `ON` | Builds `examples/geometry` for enabled backends |
| `PYRABIND_BUILD_BENCHMARKS` | `ON` | Builds C++, `pyrabind`, raw `nanobind`, raw Python C-API, and raw R benchmarks |

Either backend can be disabled cleanly — for example, `-DPYRABIND_BUILD_R=OFF` builds Python-only without requiring R or `cpp11`.

---

## 3. Installing & Using `pyrabind` in an External Project

### Install

```bash
cmake --install build-release --prefix /path/to/install
```

Installed layout:

```text
<prefix>/
├── include/pyrabind/           # Public & backend C++20 headers
├── lib/
│   ├── libpyrabind_r_runtime.a # R runtime static archive (when PYRABIND_BUILD_R=ON)
│   └── cmake/pyrabind/         # pyrabindConfig.cmake, pyrabindTargets.cmake, PyrabindTargets.cmake, FindR.cmake, FindCpp11.cmake
└── share/pyrabind/pyrabind.R   # Base-R module loader & S3 dispatch script
```

### Consume via `find_package(pyrabind)`

In your project's `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_extension LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(pyrabind CONFIG REQUIRED)

pyrabind_add_module(my_ext
    SOURCES src/bindings.cpp
    LINK_LIBRARIES my_cpp_library
)
```

### `pyrabind_add_module(...)` Signature

Defined in `cmake/PyrabindTargets.cmake`:

```cmake
pyrabind_add_module(<name>
    [NOMINSIZE]
    [OUTPUT_DIR <dir>]
    SOURCES <src1> [<src2> ...]
    [LINK_LIBRARIES <lib1> [<lib2> ...]]
)
```

- **`SOURCES`** (required): One or more C++20 translation units defining `PYRABIND_MODULE(<name>, m)`.
- **`NOMINSIZE`** (optional flag): Forwards `NOMINSIZE` to upstream `nanobind_add_module` so the Python target compiles with `-O3` rather than `nanobind`'s default `-Os` size optimization. Recommended for numerical loops and benchmarks.
- **`OUTPUT_DIR`** (optional, defaults to `${CMAKE_CURRENT_BINARY_DIR}`): Python shared module is placed in `<OUTPUT_DIR>/python/` and R shared library in `<OUTPUT_DIR>/r/`.
- **`LINK_LIBRARIES`** (optional): Additional CMake targets or libraries linked privately to both `<name>_py` and `<name>_r`.
- Automatically enables strict warnings via `pyrabind_apply_strict_warnings` (`-Wall -Wextra -Wpedantic -Wshadow`, or `/W4 /permissive-` on MSVC).

---

## 4. Loading Compiled Modules

### Python

Add `<OUTPUT_DIR>/python` to `PYTHONPATH` (or package into a wheel) and `import` normally:

```bash
PYTHONPATH=build-release/examples/geometry/python python3 examples/geometry/run_python.py
```

```python
import geometry
```

### R

Source `r/pyrabind.R` (or `<prefix>/share/pyrabind/pyrabind.R`) once and call `pyrabind_load(path)` with the path to the compiled R shared library:

```bash
Rscript examples/geometry/run_r.R r/pyrabind.R build-release/examples/geometry/r/geometry.so
```

```r
source("r/pyrabind.R")
geo <- pyrabind_load("build-release/examples/geometry/r/geometry.so")
```

`pyrabind_load(path)` registers the `pyrabind_*` S3 generics once per R session, `dyn.load`s the shared library, resolves the registered `.Call` / `.External` symbols, invokes the C++ module initializer, and returns an R environment containing all exported functions, classes, submodules, enums, and attributes.

---

## 5. Pure C++ Core Mode (`PYRABIND_CORE_ONLY`)

If you want to unit-test backend-independent `pyrabind::data_frame` code or C++ kernels without Python or R headers, link against `pyrabind::core` and define `PYRABIND_CORE_ONLY`:

```cmake
add_executable(my_core_tests test_core.cpp)
target_link_libraries(my_core_tests PRIVATE pyrabind::core)
target_compile_definitions(my_core_tests PRIVATE PYRABIND_CORE_ONLY)
```

---

## 6. Building & Previewing the Documentation Site (MkDocs)

`pyrabind` ships an **MkDocs Material** site configured in `mkdocs.yml`. To preview locally or run the strict documentation build check:

```bash
pip install -r docs/requirements.txt
mkdocs serve

# Strict build verification (matching GitHub Actions CI & Pages deploy):
mkdocs build --strict
```
