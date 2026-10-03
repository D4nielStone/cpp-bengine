cmake_minimum_required(VERSION 3.20)

project(
    BubbleEngine
    VERSION 0.1.0
    LANGUAGES C CXX
)

include(CTest)

set(BGUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGUI_TESTS ${BUILD_TESTING} CACHE BOOL "" FORCE)
add_subdirectory(external/cpp-bgui)

# The reusable engine library.
add_subdirectory(commons)
add_subdirectory(editor)

# Runtime and editor applications.
add_subdirectory(apps)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
