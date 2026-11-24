add_executable(nanorbind_bench_cpp bench_cpp.cpp)
target_link_libraries(nanorbind_bench_cpp PRIVATE nanorbind::core)
target_compile_definitions(nanorbind_bench_cpp PRIVATE NANORBIND_CORE_ONLY)
set_target_properties(nanorbind_bench_cpp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cpp"
)
nanorbind_apply_strict_warnings(nanorbind_bench_cpp)

nanorbind_add_module(bench_module SOURCES bench_module.cpp)

if(NANORBIND_BUILD_PYTHON)
    nanobind_add_module(raw_nb_bench NB_STATIC raw_nb_baseline.cpp)
    set_target_properties(raw_nb_bench PROPERTIES
        OUTPUT_NAME "raw_nb_bench"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/python"
    )
    nanorbind_apply_strict_warnings(raw_nb_bench)

    Python_add_library(raw_py_bench MODULE WITH_SOABI raw_py_baseline.cpp)
    set_target_properties(raw_py_bench PROPERTIES
        OUTPUT_NAME "raw_py_bench"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/python"
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_include_directories(raw_py_bench SYSTEM PRIVATE ${Python_INCLUDE_DIRS})
    nanorbind_apply_strict_warnings(raw_py_bench)
endif()

if(NANORBIND_BUILD_R)
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
    target_include_directories(raw_r_bench SYSTEM PRIVATE "${R_INCLUDE_DIR}")
    target_link_libraries(raw_r_bench PRIVATE R::R)
    nanorbind_apply_strict_warnings(raw_r_bench)
endif()
