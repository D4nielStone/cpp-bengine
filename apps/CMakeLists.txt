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

    add_library(editor_services STATIC
        editor/services/editor_api.cpp
        editor/services/visual_lua.cpp
        editor/services/scene_graph.cpp
        editor/panels/scene_workflow.cpp
        editor/panels/model_editor_rig.cpp
    )
    target_include_directories(editor_services PRIVATE ${EDITOR_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/project)
    target_link_libraries(editor_services PUBLIC editor_core PRIVATE commons)
endif()

if(BUILD_MCP)
    add_executable(bengine_mcp mcp/main.cpp mcp/project_snapshot.cpp mcp/creative_service.cpp
        editor/systems/project_creation_system.cpp editor/systems/runtime_process.cpp)
    target_include_directories(bengine_mcp PRIVATE ${EDITOR_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/project)
    target_link_libraries(bengine_mcp PRIVATE project_persistence)
    target_include_directories(bengine_mcp PRIVATE ${CMAKE_BINARY_DIR}/generated)
    target_include_directories(bengine_mcp SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/external/rapidjson/include)
endif()

if(BUILD_RUNTIME OR BUILD_EDITOR OR BUILD_TESTING OR BUILD_MCP)
    add_library(project_persistence STATIC project/project_persistence_system.cpp)
    target_include_directories(project_persistence PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/project)
    target_link_libraries(project_persistence PUBLIC commons)
endif()

if(BUILD_RUNTIME)
    add_executable(runtime_app runtime_app.cpp editor/systems/project_preview.cpp)
    if(WIN32)
        target_link_libraries(runtime_app PRIVATE windowscodecs ole32 uuid)
    else()
        find_package(PNG REQUIRED)
        target_link_libraries(runtime_app PRIVATE PNG::PNG)
    endif()
    target_include_directories(runtime_app PRIVATE ${EDITOR_INCLUDE_DIRS})
    target_link_libraries(runtime_app PRIVATE project_persistence)
    if(BENGINE_SINGLE_FILE_RUNTIME AND UNIX)
        target_link_options(runtime_app PRIVATE -static-libstdc++ -static-libgcc)
    endif()
    if((COMMONS_USE_RELATIVE_ASSET_PATHS OR BUILD_EDITOR) AND UNIX AND NOT APPLE)
        set_target_properties(runtime_app PROPERTIES BUILD_RPATH "$ORIGIN/lib"
            INSTALL_RPATH "$ORIGIN/lib" BUILD_WITH_INSTALL_RPATH "${COMMONS_USE_RELATIVE_ASSET_PATHS}")
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
    target_link_libraries(editor_app PRIVATE project_persistence editor_services editor_connection CURL::libcurl)
    add_dependencies(editor_app game_packager)
    if(TARGET runtime_app)
        add_dependencies(editor_app runtime_app)
        if(NOT BENGINE_DISTRIBUTION_BUILD AND NOT CMAKE_CROSSCOMPILING AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND
           CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND
           (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
            if(WIN32)
                set(export_target windows)
            else()
                set(export_target linux)
            endif()
            add_custom_target(native_export_template
                COMMAND ${CMAKE_COMMAND}
                    "-DBINARY=$<TARGET_FILE:runtime_app>"
                    "-DPACKAGER=$<TARGET_FILE:game_packager>"
                    "-DREPOSITORY=${CMAKE_SOURCE_DIR}"
                    "-DTARGET_PLATFORM=${export_target}"
                    -P "${CMAKE_SOURCE_DIR}/scripts/create_native_template.cmake"
                DEPENDS runtime_app game_packager
                COMMENT "Preparing the native project export template"
                VERBATIM)
            add_dependencies(editor_app native_export_template)
        endif()
    endif()
    target_compile_definitions(editor_app PRIVATE BENGINE_PACKAGER_PATH="$<TARGET_FILE:game_packager>")
    target_compile_definitions(editor_app PRIVATE BENGINE_REPOSITORY_ROOT="${CMAKE_SOURCE_DIR}")
    if(WIN32)
        enable_language(RC)
        configure_file("${CMAKE_SOURCE_DIR}/tools/release/bubble-engine.rc.in" editor_icon.rc @ONLY)
        target_sources(editor_app PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/editor_icon.rc")
        target_link_libraries(editor_app PRIVATE windowscodecs ole32 shell32 uuid)
    else()
        find_package(PNG REQUIRED)
        target_link_libraries(editor_app PRIVATE PNG::PNG)
    endif()

endif()
