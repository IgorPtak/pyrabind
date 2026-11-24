#pragma once

#include "pyrabind/pyrabind.hpp"

#if defined(PYRABIND_TARGET_PYTHON)
#include <nanobind/stl/list.h>
#elif defined(PYRABIND_TARGET_R)
#include "pyrabind/r/stl/list.hpp"
#endif
