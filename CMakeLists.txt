cmake_minimum_required(VERSION 3.20)

project(
    BubbleEngine
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

option(BUILD_RUNTIME "Build the standalone runtime app" ON)
option(BUILD_EDITOR "Build the editor app" ON)
option(ENABLE_SANITIZERS "Enable ASan/UBSan for development builds when supported" OFF)

if(ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fno-omit-frame-pointer -fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

set(BGUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGUI_TESTS ${BUILD_TESTING} CACHE BOOL "" FORCE)
add_subdirectory(external/cpp-bgui)

# The reusable engine library.
add_subdirectory(commons)

# Runtime and editor applications.
add_subdirectory(apps)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
