#include "systems/ui_system.hpp"

#include "glad.h"

#include <bgui.hpp>
#include <bgui_backend_gl3.hpp>
#include <bgui_backend_glfw.hpp>
#include <bgui_backend_freetype.hpp>
#include <os/style_manager.hpp>
#include <utils/theme.hpp>

#include "debugging/debug.hpp"

using namespace COMMONS_NS;

void ui_system::setup(const std::shared_ptr<ecs>&)
{
    if (m_initialized)
        return;

    bgui::set_up_gl3();
    bgui::set_up_freetype();
    bgui::style_manager::get_instance().apply_theme(bgui::light_theme());

    auto& root = bgui::get_layout();
    root.add_persistent<bgui::window>("Main Window");

    bgui::cascade_style();
    bgui::load_font_queue();
    m_initialized = true;
    debugging::emit(debug, "ui", "cpp-bgui inicializado");
    bgui::get_context().m_refresh_func = [this]() {
        bgui::glfw_update(bgui::get_context());
        bgui::load_font_queue();
        bgui::on_update();

        const GLboolean framebuffer_srgb_enabled =
            glIsEnabled(GL_FRAMEBUFFER_SRGB);
        glDisable(GL_FRAMEBUFFER_SRGB);
        bgui::gl3_clear();
        bgui::gl3_render(bgui::get_draw_data());
        if (framebuffer_srgb_enabled)
            glEnable(GL_FRAMEBUFFER_SRGB);
    };
}

void ui_system::update(const std::shared_ptr<ecs>&)
{
}

ui_system::~ui_system()
{
    if (!m_initialized)
        return;

    bgui::shutdown_gl3();
    bgui::shutdown_freetype();
}
