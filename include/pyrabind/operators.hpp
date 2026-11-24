#pragma once

#include "pyrabind/pyrabind.hpp"

#if defined(PYRABIND_TARGET_PYTHON)
#include <nanobind/operators.h>
#elif defined(PYRABIND_TARGET_R)
#include "pyrabind/r/operators.hpp"
#endif
