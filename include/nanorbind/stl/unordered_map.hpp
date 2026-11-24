#pragma once
#include "nanorbind/nanorbind.hpp"
#if defined(NANORBIND_TARGET_PYTHON)
#include <nanobind/stl/unordered_map.h>
#elif defined(NANORBIND_TARGET_R)
#include "nanorbind/r/stl/map.hpp"
#endif
