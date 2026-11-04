cmake_minimum_required(VERSION 3.20)
project(apps LANGUAGES CXX)

set(EDITOR_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/editor
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/core
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/panels
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/systems
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/tools
)

if(BUILD_EDITOR OR BUILD_TESTING)
    # The public editor layer can see the ECS headers without inheriting
    # commons' GUI, graphics backend or platform include paths and libraries.
    add_library(editor_core INTERFACE)
    target_compile_features(editor_core INTERFACE cxx_std_20)
    target_include_directories(editor_core INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/editor/core
        ${CMAKE_SOURCE_DIR}/commons/include
        ${CMAKE_SOURCE_DIR}/external/rapidjson/include
    )

    add_library(editor_services STATIC editor/services/editor_api.cpp)
    target_include_directories(editor_services PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/editor/systems)
    target_link_libraries(editor_services PUBLIC editor_core PRIVATE commons)
endif()

if(BUILD_RUNTIME OR BUILD_EDITOR OR BUILD_TESTING)
    add_library(project_persistence STATIC project/project_persistence_system.cpp)
    target_include_directories(project_persistence PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/project)
    target_link_libraries(project_persistence PUBLIC commons)
endif()

if(BUILD_RUNTIME)
    add_executable(runtime_app runtime_app.cpp)
    target_include_directories(runtime_app PRIVATE ${EDITOR_INCLUDE_DIRS})
    target_link_libraries(runtime_app PRIVATE project_persistence)
    if(BENGINE_SINGLE_FILE_RUNTIME AND UNIX)
        target_link_options(runtime_app PRIVATE -static-libstdc++ -static-libgcc)
    endif()
    if(COMMONS_USE_RELATIVE_ASSET_PATHS AND UNIX)
        set_target_properties(runtime_app PROPERTIES BUILD_RPATH "$ORIGIN/lib"
            INSTALL_RPATH "$ORIGIN/lib" BUILD_WITH_INSTALL_RPATH TRUE)
        # DT_RPATH also resolves bundled transitive dependencies.
        target_link_options(runtime_app PRIVATE "LINKER:--disable-new-dtags")
    endif()
endif()

if(BUILD_EDITOR)
    add_subdirectory(${CMAKE_SOURCE_DIR}/tools/game_packager ${CMAKE_BINARY_DIR}/game_packager)
    set_target_properties(game_packager PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/apps)
    add_executable(
        editor_app
        editor_app.cpp
        editor/systems/release_check.cpp
        editor/systems/native_file_dialog.cpp
        editor/panels/editor_ui.cpp
        editor/systems/build_process.cpp
        editor/systems/project_creation_system.cpp
        editor/systems/project_preview.cpp
        editor/systems/runtime_process.cpp
        editor/panels/runtime_tools_view.cpp
        editor/panels/game_view.cpp
        editor/panels/scene_view.cpp
        editor/presenters/scene_presenter.cpp
        editor/systems/ui_persistence_system.cpp
    )
    target_include_directories(editor_app PRIVATE ${EDITOR_INCLUDE_DIRS})
    find_package(CURL REQUIRED)
    target_link_libraries(editor_app PRIVATE project_persistence editor_services CURL::libcurl)
    add_dependencies(editor_app game_packager)
    if(TARGET runtime_app)
        add_dependencies(editor_app runtime_app)
    endif()
    target_compile_definitions(editor_app PRIVATE BENGINE_PACKAGER_PATH="$<TARGET_FILE:game_packager>")
    target_compile_definitions(editor_app PRIVATE BENGINE_REPOSITORY_ROOT="${CMAKE_SOURCE_DIR}")
    if(WIN32)
        target_link_libraries(editor_app PRIVATE windowscodecs ole32 shell32 uuid)
    else()
        find_package(PNG REQUIRED)
        target_link_libraries(editor_app PRIVATE PNG::PNG)
    endif()

endif()
