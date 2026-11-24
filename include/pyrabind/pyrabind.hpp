#pragma once

#if defined(PYRABIND_TARGET_PYTHON)
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>

namespace pyrabind {
using namespace ::nanobind;

namespace literals {
using namespace ::nanobind::literals;
} // namespace literals
} // namespace pyrabind

#define PYRABIND_MODULE(name, variable) NB_MODULE(name, variable)
#define PYRABIND_MAKE_OPAQUE(...) NB_MAKE_OPAQUE(__VA_ARGS__)

#elif defined(PYRABIND_TARGET_R)
#include "pyrabind/r/backend.hpp"

#elif defined(PYRABIND_CORE_ONLY)
// Core-only compilation mode for backend-independent C++ unit tests.

#else
#error "Define PYRABIND_TARGET_PYTHON, PYRABIND_TARGET_R, or PYRABIND_CORE_ONLY"
#endif
