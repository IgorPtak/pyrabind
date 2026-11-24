add_executable(pyrabind_test_core cpp/test_core.cpp)
target_link_libraries(pyrabind_test_core PRIVATE pyrabind::core)
target_compile_definitions(pyrabind_test_core PRIVATE PYRABIND_CORE_ONLY)
pyrabind_apply_strict_warnings(pyrabind_test_core)

add_test(NAME cpp_core_tests COMMAND pyrabind_test_core)

pyrabind_add_module(test_module SOURCES module/test_module.cpp)

if(PYRABIND_BUILD_PYTHON)
    add_test(
        NAME python_bindings_tests
        COMMAND "${Python_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/python/test_pyrabind.py"
    )
    set_tests_properties(python_bindings_tests PROPERTIES
        ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:test_module_py>"
    )
endif()

if(PYRABIND_BUILD_R)
    add_test(
        NAME r_bindings_tests
        COMMAND "${RSCRIPT_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/r/test_pyrabind.R"
                "${CMAKE_SOURCE_DIR}/r/pyrabind.R" "$<TARGET_FILE:test_module_r>"
    )
    add_test(
        NAME r_gctorture_tests
        COMMAND "${RSCRIPT_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/r/test_gctorture.R"
                "${CMAKE_SOURCE_DIR}/r/pyrabind.R" "$<TARGET_FILE:test_module_r>"
    )
endif()

find_package(Eigen3 3.3 QUIET NO_MODULE)
if(NOT Eigen3_FOUND)
    find_path(EIGEN3_INCLUDE_DIR Eigen/Core
        HINTS /opt/homebrew/include/eigen3 /usr/local/include/eigen3 /usr/include/eigen3
    )
    if(EIGEN3_INCLUDE_DIR)
        add_library(Eigen3::Eigen INTERFACE IMPORTED)
        set_target_properties(Eigen3::Eigen PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${EIGEN3_INCLUDE_DIR}"
        )
        set(Eigen3_FOUND TRUE)
    endif()
endif()

if(Eigen3_FOUND)
    pyrabind_add_module(test_eigen_module
        SOURCES eigen/test_eigen_module.cpp
        LINK_LIBRARIES Eigen3::Eigen
    )
    if(PYRABIND_BUILD_PYTHON)
        add_test(
            NAME python_eigen_tests
            COMMAND "${Python_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/eigen/test_eigen.py"
        )
        set_tests_properties(python_eigen_tests PROPERTIES
            ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:test_eigen_module_py>"
        )
    endif()
    if(PYRABIND_BUILD_R)
        add_test(
            NAME r_eigen_tests
            COMMAND "${RSCRIPT_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/eigen/test_eigen.R"
                    "${CMAKE_SOURCE_DIR}/r/pyrabind.R" "$<TARGET_FILE:test_eigen_module_r>"
        )
    endif()
endif()

if(PYRABIND_BUILD_PYTHON)
    set(_stub_dir "${CMAKE_CURRENT_BINARY_DIR}/stubgen_out")
    set(_stubgen_cmd
        "${Python_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/pyrabind_stubgen.py"
        --module test_module
        --python-path "$<TARGET_FILE_DIR:test_module_py>"
        --pyi-out "${_stub_dir}/test_module.pyi"
        --r-out "${_stub_dir}/test_module_wrappers.R"
        --verify
    )
    if(PYRABIND_BUILD_R)
        list(APPEND _stubgen_cmd --rscript "${RSCRIPT_EXECUTABLE}")
    endif()
    add_test(NAME stubgen_tests COMMAND ${_stubgen_cmd})
endif()
