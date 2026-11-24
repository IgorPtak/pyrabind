#pragma once

#include "nanorbind/nanorbind.hpp"

#if defined(NANORBIND_TARGET_PYTHON)
#include <nanobind/operators.h>
#elif defined(NANORBIND_TARGET_R)
#include "nanorbind/r/operators.hpp"
#endif
