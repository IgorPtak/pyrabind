#pragma once

#include "pyrabind/pyrabind.hpp"

#if defined(PYRABIND_TARGET_PYTHON)
#include <nanobind/make_iterator.h>
#elif defined(PYRABIND_TARGET_R)
#include "pyrabind/r/make_iterator.hpp"
#endif
