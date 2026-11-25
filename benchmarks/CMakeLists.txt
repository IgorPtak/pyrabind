add_executable(pyrabind_bench_cpp bench_cpp.cpp)
target_link_libraries(pyrabind_bench_cpp PRIVATE pyrabind::core)
target_compile_definitions(pyrabind_bench_cpp PRIVATE PYRABIND_CORE_ONLY)
set_target_properties(pyrabind_bench_cpp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cpp"
)
pyrabind_apply_strict_warnings(pyrabind_bench_cpp)

pyrabind_add_module(bench_module NOMINSIZE SOURCES bench_module.cpp)

if(PYRABIND_BUILD_PYTHON)
    nanobind_add_module(raw_nb_bench NB_STATIC NOMINSIZE raw_nb_baseline.cpp)
    set_target_properties(raw_nb_bench PROPERTIES
        OUTPUT_NAME "raw_nb_bench"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/python"
    )
    target_include_directories(raw_nb_bench PRIVATE "${CMAKE_SOURCE_DIR}/include")
    pyrabind_apply_strict_warnings(raw_nb_bench)

    Python_add_library(raw_py_bench MODULE WITH_SOABI raw_py_baseline.cpp)
    set_target_properties(raw_py_bench PROPERTIES
        OUTPUT_NAME "raw_py_bench"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/python"
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_include_directories(raw_py_bench SYSTEM PRIVATE ${Python_INCLUDE_DIRS})
    pyrabind_apply_strict_warnings(raw_py_bench)
endif()

if(PYRABIND_BUILD_R)
    add_library(raw_r_bench MODULE raw_r_baseline.cpp)
    set_target_properties(raw_r_bench PROPERTIES
        PREFIX ""
        OUTPUT_NAME "raw_r_bench"
        SUFFIX "${R_SHLIB_EXT}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/r"
        BUILD_RPATH "${R_LIB_DIR}"
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_include_directories(raw_r_bench PRIVATE "${CMAKE_SOURCE_DIR}/include")
    target_include_directories(raw_r_bench SYSTEM PRIVATE "${R_INCLUDE_DIR}")
    target_link_libraries(raw_r_bench PRIVATE R::R)
    pyrabind_apply_strict_warnings(raw_r_bench)
endif()
