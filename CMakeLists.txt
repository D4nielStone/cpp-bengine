cmake_minimum_required(VERSION 3.20)

project(
    BubbleEngine
    VERSION 0.1.1
    LANGUAGES C CXX
)

configure_file(cmake/engine_version.hpp.in ${CMAKE_BINARY_DIR}/generated/engine_version.hpp @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    add_compile_options(/utf-8)
endif()

option(BENGINE_DISTRIBUTION_BUILD "Build relocatable release binaries for the installer" OFF)
if(BENGINE_DISTRIBUTION_BUILD)
    set(COMMONS_USE_RELATIVE_ASSET_PATHS ON CACHE BOOL "" FORCE)
endif()

include(CTest)
find_package(ZLIB REQUIRED)

option(BENGINE_SINGLE_FILE_RUNTIME "Link non-system runtime dependencies statically" OFF)
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

if(BENGINE_DISTRIBUTION_BUILD AND UNIX AND NOT APPLE)
    foreach(application editor_app runtime_app game_packager)
        if(TARGET ${application})
            set_target_properties(${application} PROPERTIES
                BUILD_RPATH "$ORIGIN/lib" INSTALL_RPATH "$ORIGIN/lib"
                BUILD_WITH_INSTALL_RPATH TRUE)
            target_link_options(${application} PRIVATE "LINKER:--disable-new-dtags")
        endif()
    endforeach()
endif()
