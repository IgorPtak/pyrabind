#pragma once
#include "pyrabind/pyrabind.hpp"
#if defined(PYRABIND_TARGET_PYTHON)
#include <nanobind/stl/unordered_map.h>
#elif defined(PYRABIND_TARGET_R)
#include "pyrabind/r/stl/map.hpp"
#endif
