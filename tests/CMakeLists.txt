include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(commons_tests
    commons_tests.cpp
)

target_link_libraries(commons_tests
    PRIVATE
        GTest::gtest_main
        commons
)

target_compile_definitions(commons_tests
    PRIVATE
        BENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_include_directories(commons_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/commons/include
        ${CMAKE_SOURCE_DIR}/external/rapidjson/include
)

target_compile_features(commons_tests PRIVATE cxx_std_20)

include(GoogleTest)
gtest_discover_tests(commons_tests)

add_executable(
    editor_persistence_tests
    editor_persistence_tests.cpp
    ${CMAKE_SOURCE_DIR}/apps/editor/systems/project_creation_system.cpp
    ${CMAKE_SOURCE_DIR}/apps/editor/systems/project_persistence_system.cpp
    ${CMAKE_SOURCE_DIR}/apps/editor/systems/project_preview.cpp
)

target_link_libraries(editor_persistence_tests
    PRIVATE
        GTest::gtest_main
        commons
)

target_include_directories(editor_persistence_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/apps/editor
        ${CMAKE_SOURCE_DIR}/apps/editor/core
        ${CMAKE_SOURCE_DIR}/apps/editor/panels
        ${CMAKE_SOURCE_DIR}/apps/editor/systems
        ${CMAKE_SOURCE_DIR}/apps/editor/tools
)

target_compile_features(editor_persistence_tests PRIVATE cxx_std_20)

gtest_discover_tests(editor_persistence_tests)

add_executable(editor_interaction_tests editor_interaction_tests.cpp editor_lua_properties_tests.cpp)
target_link_libraries(editor_interaction_tests PRIVATE GTest::gtest_main commons)
target_include_directories(editor_interaction_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/editor/core
    ${CMAKE_SOURCE_DIR}/apps/editor/panels
    ${CMAKE_SOURCE_DIR}/apps/editor/systems
    ${CMAKE_SOURCE_DIR}/apps/editor/tools
)
gtest_discover_tests(editor_interaction_tests)

add_executable(editor_script_tests editor_script_tests.cpp)
target_include_directories(editor_script_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/editor/core
    ${CMAKE_SOURCE_DIR}/apps/editor/systems
)
target_link_libraries(editor_script_tests PRIVATE GTest::gtest_main commons)
gtest_discover_tests(editor_script_tests)

add_executable(editor_asset_workflow_tests editor_asset_workflow_tests.cpp)
target_include_directories(editor_asset_workflow_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/editor/core
    ${CMAKE_SOURCE_DIR}/apps/editor/panels
    ${CMAKE_SOURCE_DIR}/apps/editor/systems
    ${CMAKE_SOURCE_DIR}/apps/editor/tools
)
target_link_libraries(editor_asset_workflow_tests PRIVATE GTest::gtest_main commons)
gtest_discover_tests(editor_asset_workflow_tests)

add_executable(editor_render_tests
    editor_render_tests.cpp
    ${CMAKE_SOURCE_DIR}/apps/editor/systems/project_persistence_system.cpp
)
target_link_libraries(editor_render_tests PRIVATE GTest::gtest_main commons)
target_include_directories(editor_render_tests PRIVATE ${CMAKE_SOURCE_DIR}/apps/editor/systems)
target_compile_definitions(editor_render_tests PRIVATE BENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(editor_render_tests)

add_executable(native_file_dialog_tests
    native_file_dialog_tests.cpp
    ${CMAKE_SOURCE_DIR}/apps/editor/systems/native_file_dialog.cpp
)
target_include_directories(native_file_dialog_tests PRIVATE ${CMAKE_SOURCE_DIR}/apps/editor/systems)
target_link_libraries(native_file_dialog_tests PRIVATE GTest::gtest_main)
if(WIN32)
    target_link_libraries(native_file_dialog_tests PRIVATE ole32 shell32 uuid)
endif()
gtest_discover_tests(native_file_dialog_tests)

add_executable(ui_scale_tests ui_scale_tests.cpp)
target_include_directories(ui_scale_tests PRIVATE ${CMAKE_SOURCE_DIR}/apps/editor/systems)
target_link_libraries(ui_scale_tests PRIVATE GTest::gtest_main bgui)
gtest_discover_tests(ui_scale_tests)

add_executable(editor_menu_tests
    editor_menu_tests.cpp
    ${CMAKE_SOURCE_DIR}/apps/editor/panels/editor_ui.cpp
    ${CMAKE_SOURCE_DIR}/apps/editor/systems/ui_persistence_system.cpp
    ${CMAKE_SOURCE_DIR}/apps/editor/systems/project_persistence_system.cpp
    ${CMAKE_SOURCE_DIR}/apps/editor/systems/project_creation_system.cpp
    ${CMAKE_SOURCE_DIR}/apps/editor/systems/build_process.cpp
    ${CMAKE_SOURCE_DIR}/apps/editor/systems/native_file_dialog.cpp
)
target_include_directories(editor_menu_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/editor/core
    ${CMAKE_SOURCE_DIR}/apps/editor/panels
    ${CMAKE_SOURCE_DIR}/apps/editor/systems
)
target_link_libraries(editor_menu_tests PRIVATE GTest::gtest_main commons bgui)
target_compile_definitions(editor_menu_tests PRIVATE BENGINE_REPOSITORY_ROOT="${CMAKE_SOURCE_DIR}")
if(WIN32)
    target_link_libraries(editor_menu_tests PRIVATE ole32 shell32 uuid)
endif()
gtest_discover_tests(editor_menu_tests)

add_executable(lua_api_tests lua_api_tests.cpp)
target_link_libraries(lua_api_tests PRIVATE GTest::gtest_main commons)
target_compile_definitions(lua_api_tests PRIVATE BENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
gtest_discover_tests(lua_api_tests)

add_executable(text_editor_tests text_editor_tests.cpp)
target_link_libraries(text_editor_tests PRIVATE GTest::gtest_main bgui)
gtest_discover_tests(text_editor_tests)

add_executable(lua_completion_tests lua_completion_tests.cpp)
target_include_directories(lua_completion_tests PRIVATE ${CMAKE_SOURCE_DIR}/apps/editor/panels)
target_link_libraries(lua_completion_tests PRIVATE GTest::gtest_main bgui)
gtest_discover_tests(lua_completion_tests)

add_executable(editor_document_tests editor_document_tests.cpp)
target_include_directories(editor_document_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/editor/core
    ${CMAKE_SOURCE_DIR}/apps/editor/panels
)
target_link_libraries(editor_document_tests PRIVATE GTest::gtest_main commons)
gtest_discover_tests(editor_document_tests)

add_executable(editor_persistence_transaction_tests
    editor_persistence_transaction_tests.cpp
    persistence_integrity_tests.cpp
    ${CMAKE_SOURCE_DIR}/apps/editor/systems/project_persistence_system.cpp
)
target_include_directories(editor_persistence_transaction_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/editor/systems
)
target_link_libraries(editor_persistence_transaction_tests PRIVATE GTest::gtest_main commons)
gtest_discover_tests(editor_persistence_transaction_tests)

# Native window delivery is kept separate from the GPU-free asset workflow tests.
add_executable(window_file_drop_tests window_file_drop_tests.cpp)
target_link_libraries(window_file_drop_tests PRIVATE GTest::gtest_main commons)
target_include_directories(window_file_drop_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/editor/systems
    ${CMAKE_SOURCE_DIR}/apps/editor/panels
)
gtest_discover_tests(window_file_drop_tests)

# Essential ECS, transform and Bullet contracts; no window or OpenGL context.
add_executable(engine_foundations_tests engine_foundations_tests.cpp)
target_link_libraries(engine_foundations_tests PRIVATE GTest::gtest_main commons)
gtest_discover_tests(engine_foundations_tests)

# Drop hints must not intercept pointer events; no native window required.
add_executable(editor_overlay_input_tests editor_overlay_input_tests.cpp)
target_include_directories(editor_overlay_input_tests PRIVATE ${CMAKE_SOURCE_DIR}/apps/editor/panels)
target_link_libraries(editor_overlay_input_tests PRIVATE GTest::gtest_main commons)
gtest_discover_tests(editor_overlay_input_tests)

# Import configuration/pose/UI tests are GPU-free; one upload test requires a display.
add_executable(model_import_tests model_import_tests.cpp)
target_link_libraries(model_import_tests PRIVATE GTest::gtest_main commons)
target_include_directories(model_import_tests PRIVATE ${CMAKE_SOURCE_DIR}/apps/editor/panels)
gtest_discover_tests(model_import_tests)

# Shell/PowerShell orchestration contracts; no graphics context required.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME installer_transactions
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/installer_tests.py)
endif()
if(Python3_Interpreter_FOUND AND UNIX)
    add_test(NAME platform_build_scripts
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/platform_build_tests.py)
endif()

add_executable(build_process_tests build_process_tests.cpp
    ${CMAKE_SOURCE_DIR}/apps/editor/systems/build_process.cpp)
target_include_directories(build_process_tests PRIVATE ${CMAKE_SOURCE_DIR}/apps/editor/systems)
target_link_libraries(build_process_tests PRIVATE GTest::gtest_main)
gtest_discover_tests(build_process_tests)

# Game export and startup resolution are independent of ECS/OpenGL.
add_executable(game_export_tests game_export_tests.cpp)
target_include_directories(game_export_tests PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/commons/include
    ${CMAKE_SOURCE_DIR}/external/cpp-bgui/core/include
    ${CMAKE_SOURCE_DIR}/external/rapidjson/include)
target_link_libraries(game_export_tests PRIVATE GTest::gtest_main ZLIB::ZLIB)
gtest_discover_tests(game_export_tests)

add_executable(executable_archive_tests executable_archive_tests.cpp
    ${CMAKE_SOURCE_DIR}/apps/editor/systems/project_persistence_system.cpp)
target_include_directories(executable_archive_tests PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/apps/editor/systems)
target_link_libraries(executable_archive_tests PRIVATE GTest::gtest_main commons)
gtest_discover_tests(executable_archive_tests)
