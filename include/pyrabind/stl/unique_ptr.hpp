#pragma once

#include "pyrabind/pyrabind.hpp"

#if defined(PYRABIND_TARGET_PYTHON)
#include <nanobind/stl/unique_ptr.h>
#elif defined(PYRABIND_TARGET_R)
#include "pyrabind/r/stl/unique_ptr.hpp"
#endif
