#pragma once

#include "pyrabind/pyrabind.hpp"

#if defined(PYRABIND_TARGET_PYTHON)
#include <nanobind/stl/bind_map.h>
#elif defined(PYRABIND_TARGET_R)
#include "pyrabind/r/stl/bind_map.hpp"
#endif
