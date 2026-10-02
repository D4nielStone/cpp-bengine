#include "systems/ui_system.hpp"

#include "glad.h"

#include <bgui.hpp>
#include <bgui_backend_gl3.hpp>
#include <bgui_backend_glfw.hpp>
#include <bgui_backend_freetype.hpp>
#include <os/style_manager.hpp>
#include <utils/theme.hpp>

#include "debugging/debug.hpp"
#include "components/camera.hpp"

using namespace COMMONS_NS;

void ui_system::setup(const std::shared_ptr<ecs>& registry)
{
    if (m_initialized)
        return;

    bgui::set_up_gl3();
    bgui::set_up_freetype();
    bgui::style_manager::get_instance().apply_theme(bgui::light_theme());

    auto& root = bgui::get_layout();

    auto& framebuffer = root.add_persistent<bgui::image>();
    framebuffer.style.layout.require_width(bgui::mode::match_parent);
    framebuffer.style.layout.require_height(bgui::mode::match_parent);
    framebuffer.style.visual.visible = true;
    m_camera_image = &framebuffer;

    bool main_camera_found = false;
    registry->cada<camera>([&](const uint32_t entity) {
        if (main_camera_found)
            return;
        auto main_camera = registry->get<camera>(entity);
        if (!main_camera) {
            return;
        }

        main_camera->createFB();

        m_camera_image->set_external_texture(
            main_camera->framebuffer_texture(),
            bgui::vec2{
                static_cast<float>(bgui::get_context_size().x),
                static_cast<float>(bgui::get_context_size().y)
            },
            true
        );
        main_camera_found = true;
    });

    auto& panel = root.add_persistent<bgui::linear>(bgui::orientation::vertical);
    panel.style.layout.require_width(bgui::mode::pixel, 320.f);
    panel.style.layout.require_height(bgui::mode::match_parent);
    panel.style.layout.set_padding(16, 16);
    panel.style.visual.visible = true;

    panel.add_persistent<bgui::text>("cpp-bgui debug scene", 0.4f);
    panel.add_persistent<bgui::button>("Button", 0.4f, [] {
        debugging::emit(debug, "ui", "botao cpp-bgui pressionado");
    });
    panel.add_persistent<bgui::checkbox>("Enable input", 0.4f, true);

    bgui::cascade_style();
    bgui::load_font_queue();
    m_initialized = true;
    debugging::emit(debug, "ui", "cpp-bgui inicializado");
}

void ui_system::update(const std::shared_ptr<ecs>&)
{
    if (!m_initialized)
        return;

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
}

ui_system::~ui_system()
{
    if (!m_initialized)
        return;

    bgui::shutdown_gl3();
    bgui::shutdown_freetype();
}
