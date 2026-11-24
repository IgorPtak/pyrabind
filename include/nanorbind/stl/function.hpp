#pragma once
#include "nanorbind/nanorbind.hpp"
#if defined(NANORBIND_TARGET_PYTHON)
#include <nanobind/stl/function.h>
#elif defined(NANORBIND_TARGET_R)
#include "nanorbind/r/stl/function.hpp"
#endif
