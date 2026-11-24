#pragma once
#include "pyrabind/pyrabind.hpp"
#if defined(PYRABIND_TARGET_PYTHON)
#include <nanobind/stl/pair.h>
#elif defined(PYRABIND_TARGET_R)
#include "pyrabind/r/stl/pair.hpp"
#endif
