cmake_minimum_required(VERSION 3.20)
project(apps VERSION 0.1.0 LANGUAGES CXX)

include_directories(${CMAKE_SOURCE_DIR}/commons/include)

add_executable(runtime_app runtime_app.cpp)
target_sources(runtime_app PRIVATE editor/project_persistence_system.cpp)
target_include_directories(runtime_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(runtime_app PRIVATE commons)

add_executable(
    editor_app
    editor_app.cpp
    editor/editor_ui.cpp
    editor/project_persistence_system.cpp
    editor/runtime_process.cpp
    editor/runtime_tools_view.cpp
    editor/scene_view.cpp
    editor/ui_persistence_system.cpp
)
target_link_libraries(editor_app PRIVATE commons)