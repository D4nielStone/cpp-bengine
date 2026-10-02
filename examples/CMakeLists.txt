cmake_minimum_required(VERSION 3.16)
project(examples VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include_directories(${CMAKE_SOURCE_DIR})
add_executable(
    exemplo1 exemplo1.cpp
)

target_link_libraries(exemplo1 PRIVATE commons)