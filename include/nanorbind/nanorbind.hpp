#pragma once

#if defined(NANORBIND_TARGET_PYTHON)
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>

namespace nanorbind {
using namespace ::nanobind;

namespace literals {
using namespace ::nanobind::literals;
} // namespace literals
} // namespace nanorbind

#define NANORBIND_MODULE(name, variable) NB_MODULE(name, variable)

#elif defined(NANORBIND_TARGET_R)
#include "nanorbind/r/backend.hpp"

#elif defined(NANORBIND_CORE_ONLY)
// Core-only compilation mode for backend-independent C++ unit tests.

#else
#error "Define NANORBIND_TARGET_PYTHON, NANORBIND_TARGET_R, or NANORBIND_CORE_ONLY"
#endif
