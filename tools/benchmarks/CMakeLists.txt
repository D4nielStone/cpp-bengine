add_executable(bengine_ui_benchmark ui_benchmark.cpp)
target_link_libraries(bengine_ui_benchmark PRIVATE commons)
target_compile_definitions(bengine_ui_benchmark PRIVATE
    BENGINE_BENCHMARK_BUILD="$<CONFIG>"
    BENGINE_BENCHMARK_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")
