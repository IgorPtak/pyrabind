cmake_minimum_required(VERSION 3.20)
project(pyra_stats LANGUAGES CXX)

if(NOT TARGET pyrabind::core)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    if(SKBUILD OR NOT DEFINED PYRABIND_BUILD_PYTHON OR PYRABIND_BUILD_PYTHON)
        find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
    endif()

    find_package(pyrabind CONFIG QUIET)
    if(NOT TARGET pyrabind::core)
        get_filename_component(_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
        if(EXISTS "${_repo_root}/include/pyrabind/pyrabind.hpp")
            set(PYRABIND_BUILD_R OFF CACHE BOOL "" FORCE)
            set(PYRABIND_BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(PYRABIND_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
            set(PYRABIND_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
            add_subdirectory("${_repo_root}" "${CMAKE_CURRENT_BINARY_DIR}/_pyrabind" EXCLUDE_FROM_ALL)
        else()
            message(FATAL_ERROR "Could not locate pyrabind package or source tree")
        endif()
    endif()
endif()

if(SKBUILD)
    nanobind_add_module(pyrastats NB_STATIC src/pyra_stats.cpp)
    target_link_libraries(pyrastats PRIVATE pyrabind::python)
    pyrabind_apply_strict_warnings(pyrastats)
    install(TARGETS pyrastats LIBRARY DESTINATION pyra_stats)
else()
    pyrabind_add_module(pyrastats NOMINSIZE SOURCES src/pyra_stats.cpp)

    if(PYRABIND_BUILD_PYTHON AND PYRABIND_BUILD_TESTS)
        add_test(
            NAME example_pyra_stats_python
            COMMAND "${Python_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pyra_stats.py"
        )
        set_tests_properties(example_pyra_stats_python PROPERTIES
            ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:pyrastats_py>"
        )
    endif()

    if(PYRABIND_BUILD_R AND PYRABIND_BUILD_TESTS)
        add_test(
            NAME example_pyra_stats_r
            COMMAND "${RSCRIPT_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/rpkg/tests/run_smoke.R"
                    "${CMAKE_SOURCE_DIR}/r/pyrabind.R" "$<TARGET_FILE:pyrastats_r>"
        )
    endif()
endif()
