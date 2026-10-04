cmake_minimum_required(VERSION 3.20)

project(editor VERSION 0.1.0 LANGUAGES CXX)

file(GLOB COMMONS_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.cpp"
)

add_library(editor STATIC ${COMMONS_SOURCES})

target_include_directories(editor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(editor PUBLIC cxx_std_20)
target_link_libraries(editor PUBLIC commons)

if(WIN32)
    target_link_libraries(editor PRIVATE ole32 shell32)
endif()