cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED BINARY OR NOT DEFINED DESTINATION)
    message(FATAL_ERROR "BINARY and DESTINATION are required")
endif()
# The host's graphics driver and glibc stay system supplied. Bundle the engine's
# other shared dependencies, including transitive libraries, beside the game.
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${BINARY}"
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    PRE_EXCLUDE_REGEXES "^linux-vdso" "^ld-linux" "^lib(c|m|dl|pthread|rt|resolv|util)\\.so"
    POST_EXCLUDE_REGEXES "/lib(EGL|GL|GLX|GLdispatch|OpenGL|vulkan)[^/]*\\.so")
if(unresolved)
    message(FATAL_ERROR "Missing game libraries: ${unresolved}")
endif()
file(MAKE_DIRECTORY "${DESTINATION}")
foreach(library IN LISTS resolved)
    file(COPY "${library}" DESTINATION "${DESTINATION}" FOLLOW_SYMLINK_CHAIN)
endforeach()
