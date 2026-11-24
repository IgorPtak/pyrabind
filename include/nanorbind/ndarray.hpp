#pragma once

#include "nanorbind/nanorbind.hpp"

#if defined(NANORBIND_TARGET_PYTHON)
#include <nanobind/ndarray.h>
namespace nanorbind {
namespace device {
using namespace ::nanobind::device;
} // namespace device
} // namespace nanorbind
#elif defined(NANORBIND_TARGET_R)
#include "nanorbind/r/ndarray.hpp"
#endif
