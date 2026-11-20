cmake_minimum_required(VERSION 3.20)

execute_process(COMMAND "${PACKAGER}" template-key "${REPOSITORY}" "${TARGET_PLATFORM}" folder
    OUTPUT_VARIABLE key OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
get_filename_component(app_directory "${PACKAGER}" DIRECTORY)
set(template "${app_directory}/export_templates/${TARGET_PLATFORM}/folder")
set(staging "${template}.staging")
file(REMOVE_RECURSE "${staging}")
if(TARGET_PLATFORM STREQUAL "linux")
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DBINARY=${BINARY}" "-DDESTINATION=${staging}/lib"
        -P "${CMAKE_CURRENT_LIST_DIR}/bundle_game_libraries.cmake" COMMAND_ERROR_IS_FATAL ANY)
endif()
execute_process(COMMAND "${PACKAGER}" template-create "${BINARY}" "${staging}" "${key}" "${TARGET_PLATFORM}" folder
    COMMAND_ERROR_IS_FATAL ANY)
file(REMOVE_RECURSE "${template}")
file(RENAME "${staging}" "${template}")
