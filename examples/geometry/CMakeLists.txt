nanorbind_add_module(geometry SOURCES geometry.cpp)

if(NANORBIND_BUILD_PYTHON AND NANORBIND_BUILD_TESTS)
    add_test(
        NAME example_geometry_python
        COMMAND "${Python_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_python.py"
    )
    set_tests_properties(example_geometry_python PROPERTIES
        ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:geometry_py>"
    )
endif()

if(NANORBIND_BUILD_R AND NANORBIND_BUILD_TESTS)
    add_test(
        NAME example_geometry_r
        COMMAND "${RSCRIPT_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_r.R"
                "${CMAKE_SOURCE_DIR}/r/nanorbind.R" "$<TARGET_FILE:geometry_r>"
    )
endif()
