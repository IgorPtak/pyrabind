#pragma once
#include "nanorbind/nanorbind.hpp"
#if defined(NANORBIND_TARGET_PYTHON)
#include <nanobind/stl/array.h>
#elif defined(NANORBIND_TARGET_R)
#include "nanorbind/r/stl/array.hpp"
#endif
