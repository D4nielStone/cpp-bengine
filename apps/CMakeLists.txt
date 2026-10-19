cmake_minimum_required(VERSION 3.20)
project(apps VERSION 0.1.0 LANGUAGES CXX)

include_directories(${CMAKE_SOURCE_DIR}/commons/include)

add_executable(runtime_app runtime_app.cpp)
target_link_libraries(runtime_app PRIVATE commons)

add_executable(
    editor_app
    editor_app.cpp
    editor/editor_ui.cpp
    editor/project_persistence_system.cpp
    editor/scene_view.cpp
    editor/ui_persistence_system.cpp
)
target_link_libraries(editor_app PRIVATE commons)
