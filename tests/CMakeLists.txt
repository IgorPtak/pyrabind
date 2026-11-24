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
endif()
