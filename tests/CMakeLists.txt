add_executable(nanorbind_test_core cpp/test_core.cpp)
target_link_libraries(nanorbind_test_core PRIVATE nanorbind::core)
target_compile_definitions(nanorbind_test_core PRIVATE NANORBIND_CORE_ONLY)
nanorbind_apply_strict_warnings(nanorbind_test_core)

add_test(NAME cpp_core_tests COMMAND nanorbind_test_core)

nanorbind_add_module(test_module SOURCES module/test_module.cpp)

if(NANORBIND_BUILD_PYTHON)
    add_test(
        NAME python_bindings_tests
        COMMAND "${Python_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/python/test_nanorbind.py"
    )
    set_tests_properties(python_bindings_tests PROPERTIES
        ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:test_module_py>"
    )
endif()

if(NANORBIND_BUILD_R)
    add_test(
        NAME r_bindings_tests
        COMMAND "${RSCRIPT_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/r/test_nanorbind.R"
                "${CMAKE_SOURCE_DIR}/r/nanorbind.R" "$<TARGET_FILE:test_module_r>"
    )
endif()
