#include "systems/ui_system.hpp"

#include <glad.h>
#include <bgui.hpp>
#include <bgui_backend_freetype.hpp>
#include <bgui_backend_gl3.hpp>
#include <bgui_backend_glfw.hpp>

#include "debugging/debug.hpp"

#include <iostream>

using namespace COMMONS_NS;

void ui_system::setup(const std::shared_ptr<ecs>&)
{
    if (m_initialized)
        return;

    bgui::set_up_gl3();
    bgui::set_up_freetype();
#ifdef _WIN32
    bgui::ft_search_system_fonts("arial,Arial,ARIAL,consolas,Consolas,CONSOLAS");
    constexpr char default_font[] = "Consolas Regular";
    bgui::ft_load_system_font(default_font);
    if (bgui::font_manager::get_instance().has_font(default_font))
        bgui::font_manager::get_instance().set_default_font(default_font);
    else
        std::cerr << "[ui] Consolas Regular not found; keeping the FreeType default font.\n";
#endif
    bgui::load_font_queue();
    m_initialized = true;
    debugging::emit(debug, "ui", "cpp-bgui inicializado");

    bgui::get_context().m_refresh_func = []() {
        bgui::glfw_update(bgui::get_context());
        bgui::load_font_queue();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        bgui::on_update();

        const GLboolean framebuffer_srgb_enabled = glIsEnabled(GL_FRAMEBUFFER_SRGB);
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