#pragma once

#include "pyrabind/pyrabind.hpp"

#if defined(PYRABIND_TARGET_PYTHON)
#include <nanobind/stl/bind_vector.h>
#elif defined(PYRABIND_TARGET_R)
#include "pyrabind/r/stl/bind_vector.hpp"
#endif
