include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(commons_tests
    commons_tests.cpp
)

target_link_libraries(commons_tests
    PRIVATE
        GTest::gtest_main
        commons
)

target_compile_definitions(commons_tests
    PRIVATE
        BENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_include_directories(commons_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/commons/include
        ${CMAKE_SOURCE_DIR}/external/rapidjson/include
)

target_compile_features(commons_tests PRIVATE cxx_std_20)

include(GoogleTest)
gtest_discover_tests(commons_tests)

add_executable(
    editor_persistence_tests
    editor_persistence_tests.cpp
    ${CMAKE_SOURCE_DIR}/apps/editor/systems/project_creation_system.cpp
    ${CMAKE_SOURCE_DIR}/apps/editor/systems/project_persistence_system.cpp
    ${CMAKE_SOURCE_DIR}/apps/editor/systems/project_preview.cpp
)

target_link_libraries(editor_persistence_tests
    PRIVATE
        GTest::gtest_main
        commons
)

target_include_directories(editor_persistence_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/apps/editor
        ${CMAKE_SOURCE_DIR}/apps/editor/core
        ${CMAKE_SOURCE_DIR}/apps/editor/panels
        ${CMAKE_SOURCE_DIR}/apps/editor/systems
        ${CMAKE_SOURCE_DIR}/apps/editor/tools
)

target_compile_features(editor_persistence_tests PRIVATE cxx_std_20)

gtest_discover_tests(editor_persistence_tests)
