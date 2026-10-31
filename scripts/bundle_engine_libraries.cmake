cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED BINARY OR NOT DEFINED DESTINATION)
    message(FATAL_ERROR "BINARY and DESTINATION required")
endif()
if(WIN32)
    file(GET_RUNTIME_DEPENDENCIES EXECUTABLES "${BINARY}"
        DIRECTORIES ${LIBRARY_DIRECTORIES}
        RESOLVED_DEPENDENCIES_VAR resolved UNRESOLVED_DEPENDENCIES_VAR unresolved
        PRE_EXCLUDE_REGEXES "[Aa][Pp][Ii]-[Mm][Ss]-" "[Ee][Xx][Tt]-[Mm][Ss]-"
        POST_EXCLUDE_REGEXES ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\].*")
    if(unresolved)
        message(FATAL_ERROR "Missing engine libraries: ${unresolved}")
    endif()
    file(MAKE_DIRECTORY "${DESTINATION}")
    foreach(library IN LISTS resolved)
        file(COPY "${library}" DESTINATION "${DESTINATION}" FOLLOW_SYMLINK_CHAIN)
    endforeach()
else()
    include("${CMAKE_CURRENT_LIST_DIR}/bundle_game_libraries.cmake")
endif()
