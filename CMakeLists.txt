cmake_minimum_required(VERSION 3.20)

project(pyrabind VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(PyrabindTargets)

option(PYRABIND_BUILD_PYTHON "Build Python extension modules using nanobind" ON)
option(PYRABIND_BUILD_R "Build R shared modules using cpp11" ON)
option(PYRABIND_BUILD_TESTS "Build the pyrabind test suite" ON)
option(PYRABIND_BUILD_EXAMPLES "Build pyrabind examples" ON)
option(PYRABIND_BUILD_BENCHMARKS "Build pyrabind overhead benchmarks" ON)
option(PYRABIND_ENABLE_SANITIZERS "Enable Address and UndefinedBehavior sanitizers on non-MSVC targets" OFF)

add_library(pyrabind_core INTERFACE)
add_library(pyrabind::core ALIAS pyrabind_core)
target_include_directories(pyrabind_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(pyrabind_core INTERFACE cxx_std_20)
target_compile_definitions(pyrabind_core INTERFACE
    $<$<PLATFORM_ID:Windows>:NOMINMAX;WIN32_LEAN_AND_MEAN>
)

set(_pyrabind_export_targets pyrabind_core)

if(PYRABIND_BUILD_PYTHON)
    if(DEFINED Python3_EXECUTABLE AND NOT DEFINED Python_EXECUTABLE)
        set(Python_EXECUTABLE "${Python3_EXECUTABLE}")
    endif()

    find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)

    execute_process(
        COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
        RESULT_VARIABLE _nb_cmake_ret
        OUTPUT_VARIABLE _nb_cmake_dir
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT _nb_cmake_ret EQUAL 0 OR NOT _nb_cmake_dir)
        message(FATAL_ERROR "Could not locate nanobind CMake config via '${Python_EXECUTABLE} -m nanobind --cmake_dir'")
    endif()

    list(APPEND CMAKE_PREFIX_PATH "${_nb_cmake_dir}")
    find_package(nanobind CONFIG REQUIRED)

    add_library(pyrabind_python INTERFACE)
    add_library(pyrabind::python ALIAS pyrabind_python)
    target_link_libraries(pyrabind_python INTERFACE pyrabind_core)
    target_compile_definitions(pyrabind_python INTERFACE PYRABIND_TARGET_PYTHON)
    if(MSVC)
        target_compile_options(pyrabind_python INTERFACE /bigobj)
    endif()

    list(APPEND _pyrabind_export_targets pyrabind_python)
endif()

if(PYRABIND_BUILD_R)
    find_package(R REQUIRED)
    find_package(Cpp11 REQUIRED)

    add_library(pyrabind_r_runtime STATIC
        src/r/runtime.cpp
        src/r/conversions.cpp
        src/r/module.cpp
    )
    set_target_properties(pyrabind_r_runtime PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_include_directories(pyrabind_r_runtime SYSTEM PUBLIC
        $<BUILD_INTERFACE:${R_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${CPP11_INCLUDE_DIR}>
    )
    target_link_libraries(pyrabind_r_runtime PUBLIC pyrabind_core cpp11::cpp11 R::R)
    target_compile_definitions(pyrabind_r_runtime PUBLIC PYRABIND_TARGET_R)
    pyrabind_apply_strict_warnings(pyrabind_r_runtime)

    add_library(pyrabind_r INTERFACE)
    add_library(pyrabind::r ALIAS pyrabind_r)
    target_link_libraries(pyrabind_r INTERFACE pyrabind_r_runtime)

    list(APPEND _pyrabind_export_targets pyrabind_r_runtime pyrabind_r)
endif()

install(TARGETS ${_pyrabind_export_targets}
    EXPORT pyrabindTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/pyrabind"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.hpp"
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/r/pyrabind.R"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/pyrabind"
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindR.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindCpp11.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PyrabindTargets.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pyrabind"
)

install(EXPORT pyrabindTargets
    FILE pyrabindTargets.cmake
    NAMESPACE pyrabind::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pyrabind"
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/pyrabindConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/pyrabindConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pyrabind"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/pyrabindConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/pyrabindConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/pyrabindConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pyrabind"
)

if(PYRABIND_BUILD_TESTS)
    enable_testing()
endif()

if(PYRABIND_BUILD_EXAMPLES)
    add_subdirectory(examples/geometry)
endif()

if(PYRABIND_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(PYRABIND_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
