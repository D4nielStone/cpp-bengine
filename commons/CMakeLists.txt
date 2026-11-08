cmake_minimum_required(VERSION 3.20)

project(commons LANGUAGES CXX)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

file(GLOB COMMONS_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.cpp"
)

add_library(commons STATIC ${COMMONS_SOURCES})

option(
    COMMONS_USE_RELATIVE_ASSET_PATHS
    "Resolve bundled assets relative to the runtime working directory"
    OFF
)
if(COMMONS_USE_RELATIVE_ASSET_PATHS)
    set(COMMONS_ASSET_DIR "assets")
else()
    set(COMMONS_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")
endif()
target_compile_definitions(commons
    PUBLIC
        COMMONS_ASSET_DIR="${COMMONS_ASSET_DIR}"
        COMMONS_SHADER_ASSET_DIR="${COMMONS_ASSET_DIR}/shaders"
        COMMONS_MODEL_ASSET_DIR="${COMMONS_ASSET_DIR}/models"
)

# Public include paths used by the engine and example apps.
target_include_directories(commons
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/external
        ${CMAKE_SOURCE_DIR}/external/glad
        ${CMAKE_SOURCE_DIR}/external/rapidjson/include
        ${CMAKE_SOURCE_DIR}/external/sol2/include
)

target_compile_features(commons PUBLIC cxx_std_20)

# Link the bundled GUI library and system dependencies used by the engine.
find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(Bullet REQUIRED)
find_package(assimp REQUIRED)
find_package(Lua 5.3 REQUIRED)
if(LUA_VERSION_STRING VERSION_GREATER_EQUAL "5.5")
    message(FATAL_ERROR "The bundled sol2 requires Lua 5.3 or 5.4; use the Lua overlay in cmake/vcpkg-ports with vcpkg.")
endif()

if(TARGET bgui_core)
    target_link_libraries(commons PUBLIC bgui_core)
endif()
if(TARGET bgui_backend_freetype)
    target_link_libraries(commons PUBLIC bgui_backend_freetype)
endif()
if(TARGET bgui_backend_gl3)
    target_link_libraries(commons PUBLIC bgui_backend_gl3)
endif()
if(TARGET bgui_backend_glfw)
    target_link_libraries(commons PUBLIC bgui_backend_glfw)
endif()

target_include_directories(commons
    PUBLIC
        ${BULLET_INCLUDE_DIRS}
        ${ASSIMP_INCLUDE_DIRS}
        ${LUA_INCLUDE_DIR}
)

target_link_libraries(commons
    PUBLIC
        Threads::Threads
        ZLIB::ZLIB
        OpenGL::GL
        glfw
        ${BULLET_LIBRARIES}
        ${ASSIMP_LIBRARIES}
        ${LUA_LIBRARIES}
)

# Native hover notifications complement GLFW's portable file-drop callback.
if(WIN32)
    target_link_libraries(commons PRIVATE ole32 shell32 uuid)
elseif(UNIX AND NOT APPLE)
    find_package(X11 QUIET)
    if(X11_FOUND)
        target_compile_definitions(commons PRIVATE BENGINE_X11_FILE_DRAG)
        target_link_libraries(commons PRIVATE X11::X11)
    endif()
endif()

# Build the playback backend into the runtime without an extra shared library.
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(miniaudio
    URL https://codeload.github.com/mackron/miniaudio/tar.gz/refs/tags/0.11.25
    URL_HASH SHA256=b900edcffe979816e2560a0580b9b1216d674b4f17fbadeca8f777a7f8ab0274
    SOURCE_SUBDIR unused
)
FetchContent_MakeAvailable(miniaudio)

add_library(bengine_audio_backend STATIC src/audio/miniaudio_backend.c)
target_include_directories(bengine_audio_backend SYSTEM PUBLIC "${miniaudio_SOURCE_DIR}")
target_compile_definitions(bengine_audio_backend PUBLIC
    MA_NO_NULL MA_NO_RESOURCE_MANAGER MA_NO_ENCODING)
target_link_libraries(bengine_audio_backend PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX)
    target_link_libraries(bengine_audio_backend PRIVATE m)
endif()
target_link_libraries(commons PRIVATE bengine_audio_backend)
