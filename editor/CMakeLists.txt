cmake_minimum_required(VERSION 3.20)

project(editor VERSION 0.1.0 LANGUAGES CXX)

add_library(editor STATIC
    src/editor_system.cpp
)

target_include_directories(editor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(editor PUBLIC cxx_std_20)
target_link_libraries(editor PUBLIC commons)