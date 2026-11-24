#pragma once
#include "pyrabind/pyrabind.hpp"
#if defined(PYRABIND_TARGET_PYTHON)
#include <nanobind/stl/tuple.h>
#elif defined(PYRABIND_TARGET_R)
#include "pyrabind/r/stl/tuple.hpp"
#endif
