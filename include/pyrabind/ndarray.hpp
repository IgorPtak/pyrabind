#pragma once

#include "pyrabind/pyrabind.hpp"

#if defined(PYRABIND_TARGET_PYTHON)
#include <nanobind/ndarray.h>
namespace pyrabind {
namespace device {
using namespace ::nanobind::device;
} // namespace device
} // namespace pyrabind
#elif defined(PYRABIND_TARGET_R)
#include "pyrabind/r/ndarray.hpp"
#endif
