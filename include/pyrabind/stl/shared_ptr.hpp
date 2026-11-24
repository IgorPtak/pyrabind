#pragma once

#include "pyrabind/pyrabind.hpp"

#if defined(PYRABIND_TARGET_PYTHON)
#include <nanobind/stl/shared_ptr.h>
#elif defined(PYRABIND_TARGET_R)
#include "pyrabind/r/stl/shared_ptr.hpp"
#endif
