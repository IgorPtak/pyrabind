# Locate the header-only cpp11 package installed inside the active R library.

if(NOT DEFINED CPP11_INCLUDE_DIR)
    find_package(R QUIET)
    if(RSCRIPT_EXECUTABLE)
        execute_process(
            COMMAND "${RSCRIPT_EXECUTABLE}" -e "cat(system.file('include', package = 'cpp11'))"
            OUTPUT_VARIABLE _cpp11_r_inc
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_cpp11_r_inc AND EXISTS "${_cpp11_r_inc}/cpp11.hpp")
            set(CPP11_INCLUDE_DIR "${_cpp11_r_inc}" CACHE PATH "Path to cpp11 headers")
        endif()
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Cpp11
    REQUIRED_VARS CPP11_INCLUDE_DIR
)

if(Cpp11_FOUND AND NOT TARGET cpp11::cpp11)
    add_library(cpp11::cpp11 INTERFACE IMPORTED)
    set_target_properties(cpp11::cpp11 PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CPP11_INCLUDE_DIR}"
        INTERFACE_COMPILE_DEFINITIONS "R_NO_REMAP;STRICT_R_HEADERS"
    )
endif()
