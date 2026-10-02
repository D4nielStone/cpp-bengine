#include "os/window.hpp"

#include <stdexcept>

#include <glad.h>
#include <bgui.hpp>
#include <bgui_backend_glfw.hpp>

using namespace COMMONS_NS;

namespace {
    void initialize_opengl_state() {
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
            throw std::runtime_error("Failed to initialize OpenGL loader.");
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_FRAMEBUFFER_SRGB);
    }
}

window::window(const std::string& title, const int width, const int height)
    : m_ecs(std::make_shared<ecs>()) {
    m_window = bgui::set_up_glfw(width, height, title.c_str());

    try {
        initialize_opengl_state();
        m_interface = std::make_unique<bgui::scoped_interface>();
    } catch (...) {
        if (m_window) {
            bgui::shutdown_glfw();
            m_window = nullptr;
        }
        throw;
    }
}

window::~window() {
    bgui::get_context().m_refresh_func = {};
    m_systems.clear();
    m_interface.reset();

    if (m_window) {
        bgui::shutdown_glfw();
        m_window = nullptr;
    }
}

void window::add(const std::shared_ptr<system>& instance) {
    if (!instance) {
        throw std::invalid_argument("Cannot add a null system to the window.");
    }

    instance->setup(m_ecs);
    m_systems.push_back(instance);
}

std::shared_ptr<ecs> window::get_ecs() const noexcept {
    return m_ecs;
}

double window::delta_time() const noexcept {
    return m_time.delta.count();
}

void window::refresh() {
    if (!m_window || glfwWindowShouldClose(m_window)) {
        return;
    }

    m_time.calculateDT();

    for (const auto& instance : m_systems) {
        if (instance) {
            instance->update(m_ecs);
        }
    }

}

void window::loop() {
    if (!m_window) {
        throw std::logic_error("The window is not initialized.");
    }

    if (m_looping) {
        throw std::logic_error("The window loop is already running.");
    }

    auto& refresh_callback = bgui::get_context().m_refresh_func;
    const auto previous_callback = refresh_callback;
    refresh_callback = [this, previous_callback] {
        refresh();
        if (previous_callback) {
            previous_callback();
        }
        bgui::swap_glfw();
    };
    m_looping = true;

    try {
        bgui::glfw_main_loop();
    } catch (...) {
        refresh_callback = previous_callback;
        m_looping = false;
        throw;
    }

    refresh_callback = previous_callback;
    m_looping = false;
} 