# AGENTS.md — Contributor & Agent Guidelines

This document defines the rules that every contributor and coding agent must follow in `nanorbind`. Also read [include/nanorbind/r/AGENTS.md](include/nanorbind/r/AGENTS.md) before changing R backend code.

## Core Goal

`nanorbind` compiles a single C++20 binding file (`NANORBIND_MODULE(...)`, using upstream `nanobind` idioms) into native **Python** and **R** extension modules with zero target conditionals in user code:

- **Python**: direct pass-through to `nanobind` (`using namespace ::nanobind`). Do not introduce wrapper layers or heap allocations in Python bindings.
- **R**: `cpp11`-powered C++20 runtime with fast `.External` dispatch and pre-indexed R class environments (`r/nanorbind.R`), providing matching overload resolution, ownership policies, zero-copy `ndarray`, and `data_frame` parity.

## Mandatory Rules

1. **English only**: Use English across all source files, comments, tests, benchmarks, docs, and commit messages.
2. **No backend `#ifdef`s in bindings**: Keep `NANORBIND_TARGET_PYTHON`, `NANORBIND_TARGET_R`, `<Python.h>`, and `<R.h>` out of user modules, `dataframe_types.hpp`, `examples/`, `tests/module/`, and `benchmarks/bench_module.cpp`.
3. **Minimal R runtime dependencies**: `r/nanorbind.R` and core R tests must work on plain base R without third-party runtime CRAN packages.
4. **Zero compiler warnings**: All targets build with `-Wall -Wextra -Wpedantic -Wshadow` (`/W4 /permissive-` on MSVC) without warnings.
5. **Formatting & verification**: Keep C++ files formatted with `clang-format`, verify Release builds and `ctest`, and commit using Conventional Commits.
