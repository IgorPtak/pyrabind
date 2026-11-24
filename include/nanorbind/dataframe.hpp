#pragma once

#include "nanorbind/dataframe_types.hpp"
#include "nanorbind/nanorbind.hpp"

#if defined(NANORBIND_TARGET_PYTHON)
#include "nanorbind/python/dataframe.hpp"
#elif defined(NANORBIND_TARGET_R)
#include "nanorbind/r/dataframe.hpp"
#endif
