#pragma once
#include "nanorbind/nanorbind.hpp"
#if defined(NANORBIND_TARGET_PYTHON)
#include <nanobind/stl/pair.h>
#elif defined(NANORBIND_TARGET_R)
#include "nanorbind/r/stl/pair.hpp"
#endif
