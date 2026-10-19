#include "os/window.hpp"

#include <filesystem>
#include <optional>
#include <stdexcept>

#include <glad.h>
#include <bgui.hpp>
#include <bgui_backend_glfw.hpp>
#include <os/style_manager.hpp>

namespace {
    template<typename T>
    void override_value(std::optional<T>& target, const std::optional<T>& value) {
        if (value)
            target = value;
    }

    void override_color(bgui::state_color& target, const bgui::state_color& value) {
        override_value(target.normal, value.normal);
        override_value(target.hover, value.hover);
        override_value(target.pressed, value.pressed);
        override_value(target.focused, value.focused);
        override_value(target.disabled, value.disabled);
    }

    void override_style(bgui::style& target, const bgui::style& value) {
        override_value(target.layout.size_mode, value.layout.size_mode);
        override_value(target.layout.align, value.layout.align);
        override_value(target.layout.margin, value.layout.margin);
        override_value(target.layout.padding, value.layout.padding);
        override_value(target.layout.limit_min, value.layout.limit_min);
        override_value(target.layout.limit_max, value.layout.limit_max);
        override_value(target.layout.size, value.layout.size);
        override_value(target.layout.ori, value.layout.ori);

        override_color(target.visual.background, value.visual.background);
        override_color(target.visual.border, value.visual.border);
        override_color(target.visual.text, value.visual.text);
        override_value(target.visual.border_radius, value.visual.border_radius);
        override_value(target.visual.border_size, value.visual.border_size);
        override_value(target.visual.font, value.visual.font);
        override_value(target.visual.visible, value.visual.visible);
    }

    void apply_editor_theme_overrides(bgui::theme& theme) {
        const auto override_path =
            std::filesystem::path(COMMONS_ASSET_DIR) /
            "themes/editor-dark.theme";
        if (!std::filesystem::is_regular_file(override_path))
            throw std::runtime_error(
                "[ui] Could not find editor theme overrides: " +
                override_path.string()
            );

        const auto overrides = bgui::load_theme(override_path.string());
        override_style(theme.base, overrides.base);

        const auto apply_styles = [](auto& target, const auto& source) {
            for (const auto& [name, value] : source)
                override_style(target[name], value);
        };
        apply_styles(theme.types, overrides.types);
        apply_styles(theme.classes, overrides.classes);
        apply_styles(theme.ids, overrides.ids);
    }

    bgui::theme editor_theme() {
        auto theme = bgui::dark_theme();
        apply_editor_theme_overrides(theme);
        return theme;
    }

}

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
    bgui::style_manager::get_instance().apply_theme(editor_theme());

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