cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED BINARY OR NOT DEFINED DESTINATION)
    message(FATAL_ERROR "BINARY and DESTINATION are required")
endif()
# Graphics drivers, glibc and the C++ runtime stay system supplied. An older
# bundled C++ runtime can prevent newer host graphics drivers from loading. Bundle the engine's
# other shared dependencies, including transitive libraries, beside the game.
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${BINARY}"
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    PRE_EXCLUDE_REGEXES "^linux-vdso" "^ld-linux" "^lib(c|m|dl|pthread|rt|resolv|util|stdc[+][+]|gcc_s)\\.so"
    POST_EXCLUDE_REGEXES "/lib(EGL|GL|GLX|GLdispatch|OpenGL|vulkan)[^/]*\\.so")
if(unresolved)
    message(FATAL_ERROR "Missing game libraries: ${unresolved}")
endif()
if(CHECK_SINGLE)
    foreach(library IN LISTS resolved)
        get_filename_component(name "${library}" NAME)
        if(NOT name MATCHES "^lib(X[^/]*|xcb[^/]*|xkbcommon[^/]*|wayland[^/]*|decor[^/]*|bsd|md)\\.so")
            message(FATAL_ERROR "Single executable runtime still needs a companion library: ${name}")
        endif()
    endforeach()
    return()
endif()
file(MAKE_DIRECTORY "${DESTINATION}")
foreach(library IN LISTS resolved)
    file(COPY "${library}" DESTINATION "${DESTINATION}" FOLLOW_SYMLINK_CHAIN)
endforeach()
