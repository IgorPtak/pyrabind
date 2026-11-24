# Dual-target CMake helpers compiling one C++20 binding file into Python (nanobind) and R (cpp11).

include_guard(GLOBAL)

function(nanorbind_apply_strict_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    endif()
endfunction()

function(nanorbind_add_module name)
    cmake_parse_arguments(ARG "" "OUTPUT_DIR" "SOURCES;LINK_LIBRARIES" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "nanorbind_add_module(${name}): SOURCES is required")
    endif()

    if(ARG_OUTPUT_DIR)
        set(_out_dir "${ARG_OUTPUT_DIR}")
    else()
        set(_out_dir "${CMAKE_CURRENT_BINARY_DIR}")
    endif()

    if(NANORBIND_BUILD_PYTHON)
        nanobind_add_module(${name}_py NB_STATIC ${ARG_SOURCES})
        set_target_properties(${name}_py PROPERTIES
            OUTPUT_NAME "${name}"
            LIBRARY_OUTPUT_DIRECTORY "${_out_dir}/python"
        )
        target_link_libraries(${name}_py PRIVATE nanorbind::python ${ARG_LINK_LIBRARIES})
        nanorbind_apply_strict_warnings(${name}_py)
    endif()

    if(NANORBIND_BUILD_R)
        add_library(${name}_r MODULE ${ARG_SOURCES})
        set_target_properties(${name}_r PROPERTIES
            PREFIX ""
            OUTPUT_NAME "${name}"
            SUFFIX "${R_SHLIB_EXT}"
            LIBRARY_OUTPUT_DIRECTORY "${_out_dir}/r"
            BUILD_RPATH "${R_LIB_DIR}"
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
        )
        target_link_libraries(${name}_r PRIVATE nanorbind::r ${ARG_LINK_LIBRARIES})
        nanorbind_apply_strict_warnings(${name}_r)
    endif()
endfunction()
