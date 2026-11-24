# Locate Rscript, R headers, shared library directory, and platform extension.

find_program(RSCRIPT_EXECUTABLE NAMES Rscript Rscript.exe)

if(RSCRIPT_EXECUTABLE)
    execute_process(
        COMMAND "${RSCRIPT_EXECUTABLE}" -e "cat(R.home('include'))"
        OUTPUT_VARIABLE R_INCLUDE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND "${RSCRIPT_EXECUTABLE}" -e "cat(R.home('lib'))"
        OUTPUT_VARIABLE R_LIB_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND "${RSCRIPT_EXECUTABLE}" -e "cat(.Platform$dynlib.ext)"
        OUTPUT_VARIABLE R_SHLIB_EXT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND "${RSCRIPT_EXECUTABLE}" -e "cat(R.home('bin'))"
        OUTPUT_VARIABLE R_BIN_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

if(R_INCLUDE_DIR)
    file(TO_CMAKE_PATH "${R_INCLUDE_DIR}" R_INCLUDE_DIR)
endif()
if(R_LIB_DIR)
    file(TO_CMAKE_PATH "${R_LIB_DIR}" R_LIB_DIR)
endif()
if(R_BIN_DIR)
    file(TO_CMAKE_PATH "${R_BIN_DIR}" R_BIN_DIR)
endif()

if(NOT R_SHLIB_EXT)
    if(WIN32)
        set(R_SHLIB_EXT ".dll")
    else()
        set(R_SHLIB_EXT ".so")
    endif()
endif()

find_library(R_LIBRARY NAMES R HINTS "${R_LIB_DIR}" "${R_BIN_DIR}/x64" "${R_BIN_DIR}" NO_DEFAULT_PATH)
if(NOT R_LIBRARY)
    find_library(R_LIBRARY NAMES R HINTS "${R_LIB_DIR}" "${R_BIN_DIR}/x64" "${R_BIN_DIR}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(R
    REQUIRED_VARS RSCRIPT_EXECUTABLE R_INCLUDE_DIR R_LIB_DIR R_LIBRARY
)

if(R_FOUND AND NOT TARGET R::R)
    add_library(R::R INTERFACE IMPORTED)
    set_target_properties(R::R PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${R_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${R_LIBRARY}"
    )
endif()
