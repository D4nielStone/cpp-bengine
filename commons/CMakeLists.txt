cmake_minimum_required(VERSION 3.20)

project(commons VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

file(GLOB COMMONS_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.cpp"
)

add_library(commons STATIC ${COMMONS_SOURCES})

set(COMMONS_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")
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
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/external
        ${CMAKE_SOURCE_DIR}/external/glad
        ${CMAKE_SOURCE_DIR}/external/rapidjson/include
        ${CMAKE_SOURCE_DIR}/external/sol2/include
)

target_compile_features(commons PUBLIC cxx_std_20)

# Link the bundled GUI library and system dependencies used by the engine.
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(Bullet REQUIRED)
find_package(assimp REQUIRED)
find_package(Lua REQUIRED)

if(TARGET bgui)
    target_link_libraries(commons PUBLIC bgui)
endif()

target_include_directories(commons
    PUBLIC
        ${BULLET_INCLUDE_DIRS}
        ${ASSIMP_INCLUDE_DIRS}
        ${LUA_INCLUDE_DIR}
)

target_link_libraries(commons
    PUBLIC
        OpenGL::GL
        glfw
        ${BULLET_LIBRARIES}
        ${ASSIMP_LIBRARIES}
        ${LUA_LIBRARIES}
)
