#include <gtest/gtest.h>
#include "profiling/gpu_timer.hpp"
#include "profiling/profiler.hpp"
#include <glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <thread>
#include <memory>

TEST(ProfilerGPU, AsyncNestedTimersPublishOriginFrameAndPreserveBindings) {
#if !BENGINE_PROFILING
    GTEST_SKIP() << "Profiling disabled at build time";
#else
    if (!glfwInit()) GTEST_SKIP() << "No GLFW display available";
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(32, 32, "Profiler test", nullptr, nullptr);
    if (!window) { glfwTerminate(); GTEST_SKIP() << "OpenGL 3.3 unavailable"; }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        glfwDestroyWindow(window); glfwTerminate(); FAIL() << "OpenGL loader failed";
    }
    auto& p = Bubble::profiler::instance(); p.enabled = true; p.next_frame(.01);
    const auto origin = p.frame_id.load();
    {
        Bubble::gpu_timer outer, inner("test pass");
        GLint before{}, after{}; glGetIntegerv(GL_CURRENT_PROGRAM, &before);
        {
            Bubble::gpu_scope a(outer);
            Bubble::gpu_scope b(inner);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        glFlush();
        bool found = false;
        for (int i=0; i<100 && !found; ++i) {
            p.next_frame(.001);
            { Bubble::gpu_scope a(outer); Bubble::gpu_scope b(inner); }
            glFlush();
            const auto frames = p.snapshot();
            for (const auto& f : frames) if (f.id == origin && f.gpu_ms >= 0) {
                for (const auto& [name, ms] : f.gpu_passes) if (name == "test pass") {
                    found = true; EXPECT_GE(ms, 0); EXPECT_GE(f.gpu_ms, ms);
                }
            }
            if (!found) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        EXPECT_TRUE(found) << "Timer results not published asynchronously";
        glGetIntegerv(GL_CURRENT_PROGRAM, &after); EXPECT_EQ(before, after);
        EXPECT_EQ(glGetError(), GL_NO_ERROR);
    }
    // Also exercise destruction after the owning context is gone.
    auto late = std::make_unique<Bubble::gpu_timer>();
    { Bubble::gpu_scope scope(*late); }
    glfwDestroyWindow(window); late.reset(); glfwTerminate();
#endif
}
