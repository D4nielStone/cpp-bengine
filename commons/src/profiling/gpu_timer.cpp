#include "profiling/gpu_timer.hpp"
#include "profiling/profiler.hpp"
#include <glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
namespace Bubble {
namespace {
constexpr GLenum timestamp_query = 0x8E28; // GL_TIMESTAMP (core 3.3 / ARB_timer_query).
using query_counter = void (APIENTRY*)(GLuint, GLenum);
using query_result = void (APIENTRY*)(GLuint, GLenum, GLuint64*);
}
gpu_timer::~gpu_timer() {
#if BENGINE_PROFILING
    if (context_ && glfwGetCurrentContext() == context_)
        for (auto& s : samples_) if (s.query[0]) glDeleteQueries(2, s.query);
#endif
}
bool gpu_timer::begin() {
#if BENGINE_PROFILING
    auto* context = glfwGetCurrentContext();
    if (active_ || !context || (context_ && context_ != context) || !glad_glGenQueries || !glad_glGetQueryObjectiv) return false;
    if (!context_) {
        context_ = context;
        const int major = glfwGetWindowAttrib(context, GLFW_CONTEXT_VERSION_MAJOR);
        const int minor = glfwGetWindowAttrib(context, GLFW_CONTEXT_VERSION_MINOR);
        if (major > 3 || (major == 3 && minor >= 3) || glfwExtensionSupported("GL_ARB_timer_query")) {
            query_counter_ = glfwGetProcAddress("glQueryCounter");
            query_result_ = glfwGetProcAddress("glGetQueryObjectui64v");
        }
    }
    if (!query_counter_ || !query_result_) return false;
    for (auto& s : samples_) {
        if (s.pending) {
            GLint ready = GL_FALSE;
            glGetQueryObjectiv(s.query[1], GL_QUERY_RESULT_AVAILABLE, &ready);
            if (!ready) continue;
            GLuint64 start{}, end{};
            reinterpret_cast<query_result>(query_result_)(s.query[0], GL_QUERY_RESULT, &start);
            reinterpret_cast<query_result>(query_result_)(s.query[1], GL_QUERY_RESULT, &end);
            if (end >= start) profiler::instance().gpu(s.frame, static_cast<double>(end-start)/1e6, pass_);
            s.pending = false;
        }
    }
    if (!profiler::instance().enabled.load(std::memory_order_relaxed)) return false;
    for (auto& s : samples_) if (!s.pending) {
        if (!s.query[0]) glGenQueries(2, s.query);
        s.frame = profiler::instance().frame_id.load(std::memory_order_relaxed);
        reinterpret_cast<query_counter>(query_counter_)(s.query[0], timestamp_query); active_ = &s; break;
    }
    return active_ != nullptr;
#else
    return false;
#endif
}
void gpu_timer::end() {
#if BENGINE_PROFILING
    if (!active_) return;
    if (glfwGetCurrentContext() != context_) { active_ = nullptr; return; }
    reinterpret_cast<query_counter>(query_counter_)(active_->query[1], timestamp_query);
    active_->pending = true; active_ = nullptr;
#endif
}
}
