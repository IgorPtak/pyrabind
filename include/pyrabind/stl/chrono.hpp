#pragma once

#include "pyrabind/pyrabind.hpp"

#if defined(PYRABIND_TARGET_PYTHON)
#include <nanobind/stl/chrono.h>
#elif defined(PYRABIND_TARGET_R)
#include "pyrabind/r/stl/chrono.hpp"
#endif
