#pragma once

#include "pyrabind/dataframe_types.hpp"
#include "pyrabind/pyrabind.hpp"

#if defined(PYRABIND_TARGET_PYTHON)
#include "pyrabind/python/dataframe.hpp"
#elif defined(PYRABIND_TARGET_R)
#include "pyrabind/r/dataframe.hpp"
#endif
