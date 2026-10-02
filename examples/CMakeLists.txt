cmake_minimum_required(VERSION 3.16)
project(examples VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include_directories(${CMAKE_SOURCE_DIR})

add_executable(cube_example cube_example.cpp)
target_link_libraries(cube_example PRIVATE commons)

add_executable(editor_example editor_example.cpp)
target_link_libraries(editor_example PRIVATE commons editor)