#pragma once
#include "pyrabind/pyrabind.hpp"
#if defined(PYRABIND_TARGET_PYTHON)
#include <nanobind/stl/variant.h>
#elif defined(PYRABIND_TARGET_R)
#include "pyrabind/r/stl/variant.hpp"
#endif
