cmake_minimum_required(VERSION 3.20)
project(apps VERSION 0.1.0 LANGUAGES CXX)

include_directories(${CMAKE_SOURCE_DIR}/commons/include)

set(EDITOR_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/editor
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/core
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/panels
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/systems
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/tools
)

add_executable(runtime_app runtime_app.cpp)
target_sources(runtime_app PRIVATE editor/systems/project_persistence_system.cpp)
target_include_directories(runtime_app PRIVATE ${EDITOR_INCLUDE_DIRS})
target_link_libraries(runtime_app PRIVATE commons)

add_executable(
    editor_app
    editor_app.cpp
    editor/panels/editor_ui.cpp
    editor/systems/build_process.cpp
    editor/systems/project_creation_system.cpp
    editor/systems/project_persistence_system.cpp
    editor/systems/project_preview.cpp
    editor/systems/runtime_process.cpp
    editor/panels/runtime_tools_view.cpp
    editor/panels/scene_view.cpp
    editor/systems/ui_persistence_system.cpp
)
target_include_directories(editor_app PRIVATE ${EDITOR_INCLUDE_DIRS})
target_link_libraries(editor_app PRIVATE commons)
if(WIN32)
    target_link_libraries(editor_app PRIVATE windowscodecs ole32)
else()
    find_package(PNG REQUIRED)
    target_link_libraries(editor_app PRIVATE PNG::PNG)
endif()
