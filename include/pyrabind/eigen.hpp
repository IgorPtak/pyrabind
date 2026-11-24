#pragma once

#include "pyrabind/pyrabind.hpp"

#if defined(PYRABIND_TARGET_PYTHON)
#include <nanobind/eigen/dense.h>
#elif defined(PYRABIND_TARGET_R)
#include "pyrabind/r/eigen.hpp"
#endif
