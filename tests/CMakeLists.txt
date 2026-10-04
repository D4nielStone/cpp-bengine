add_executable(commons_tests
    commons_tests.cpp
)

target_link_libraries(commons_tests
    PRIVATE
        GTest::gtest_main
        commons
)

target_include_directories(commons_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/commons/include
        ${CMAKE_SOURCE_DIR}/external/rapidjson/include
)

include(GoogleTest)
gtest_discover_tests(commons_tests)

add_executable(editor_tests
    editor_assets_tests.cpp
)

target_link_libraries(editor_tests
    PRIVATE
        GTest::gtest_main
        editor
)

gtest_discover_tests(editor_tests)