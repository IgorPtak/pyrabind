cmake_minimum_required(VERSION 3.20)

project(nanorbind VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(NanorbindTargets)

option(NANORBIND_BUILD_PYTHON "Build Python extension modules using nanobind" ON)
option(NANORBIND_BUILD_R "Build R shared modules using cpp11" ON)
option(NANORBIND_BUILD_TESTS "Build the nanorbind test suite" ON)
option(NANORBIND_BUILD_EXAMPLES "Build nanorbind examples" ON)
option(NANORBIND_BUILD_BENCHMARKS "Build nanorbind overhead benchmarks" ON)

add_library(nanorbind_core INTERFACE)
add_library(nanorbind::core ALIAS nanorbind_core)
target_include_directories(nanorbind_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(nanorbind_core INTERFACE cxx_std_20)

set(_nanorbind_export_targets nanorbind_core)

if(NANORBIND_BUILD_PYTHON)
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

    add_library(nanorbind_python INTERFACE)
    add_library(nanorbind::python ALIAS nanorbind_python)
    target_link_libraries(nanorbind_python INTERFACE nanorbind_core)
    target_compile_definitions(nanorbind_python INTERFACE NANORBIND_TARGET_PYTHON)
    if(MSVC)
        target_compile_options(nanorbind_python INTERFACE /bigobj)
    endif()

    list(APPEND _nanorbind_export_targets nanorbind_python)
endif()

if(NANORBIND_BUILD_R)
    find_package(R REQUIRED)
    find_package(Cpp11 REQUIRED)

    add_library(nanorbind_r_runtime STATIC
        src/r/runtime.cpp
        src/r/conversions.cpp
        src/r/module.cpp
    )
    set_target_properties(nanorbind_r_runtime PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_include_directories(nanorbind_r_runtime SYSTEM PUBLIC
        $<BUILD_INTERFACE:${R_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${CPP11_INCLUDE_DIR}>
    )
    target_link_libraries(nanorbind_r_runtime PUBLIC nanorbind_core cpp11::cpp11 R::R)
    target_compile_definitions(nanorbind_r_runtime PUBLIC NANORBIND_TARGET_R)
    nanorbind_apply_strict_warnings(nanorbind_r_runtime)

    add_library(nanorbind_r INTERFACE)
    add_library(nanorbind::r ALIAS nanorbind_r)
    target_link_libraries(nanorbind_r INTERFACE nanorbind_r_runtime)

    list(APPEND _nanorbind_export_targets nanorbind_r_runtime nanorbind_r)
endif()

install(TARGETS ${_nanorbind_export_targets}
    EXPORT nanorbindTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/nanorbind"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.hpp"
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/r/nanorbind.R"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/nanorbind"
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindR.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindCpp11.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NanorbindTargets.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/nanorbind"
)

install(EXPORT nanorbindTargets
    FILE nanorbindTargets.cmake
    NAMESPACE nanorbind::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/nanorbind"
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/nanorbindConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/nanorbindConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/nanorbind"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/nanorbindConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/nanorbindConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/nanorbindConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/nanorbind"
)

if(NANORBIND_BUILD_TESTS)
    enable_testing()
endif()

if(NANORBIND_BUILD_EXAMPLES)
    add_subdirectory(examples/geometry)
endif()

if(NANORBIND_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(NANORBIND_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
