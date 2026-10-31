#pragma once
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

namespace test_support {
inline bool supports_opengl33() {
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* probe = glfwCreateWindow(16, 16, "OpenGL capability check", nullptr, nullptr);
    if (!probe) { glfwTerminate(); return false; }
    glfwDestroyWindow(probe);
    return true;
}
}
