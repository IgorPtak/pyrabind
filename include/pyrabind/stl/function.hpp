#pragma once
#include "pyrabind/pyrabind.hpp"
#if defined(PYRABIND_TARGET_PYTHON)
#include <nanobind/stl/function.h>
#elif defined(PYRABIND_TARGET_R)
#include "pyrabind/r/stl/function.hpp"
#endif
