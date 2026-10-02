#include "systems/ui_system.hpp"

#include <algorithm>

#include "glad.h"
#include "components/camera.hpp"

#include <bgui.hpp>
#include <bgui_backend_gl3.hpp>
#include <bgui_backend_glfw.hpp>
#include <bgui_backend_freetype.hpp>
#include <os/style_manager.hpp>
#include <utils/theme.hpp>

#include "debugging/debug.hpp"

using namespace COMMONS_NS;

void ui_system::setup(const std::shared_ptr<ecs>&reg)
{
    if (m_initialized)
        return;

    bgui::set_up_gl3();
    bgui::set_up_freetype();
    bgui::style_manager::get_instance().apply_theme(bgui::dark_theme());

    auto& root = bgui::get_layout();
    auto& dock = root.add_persistent<bgui::dock>();
    auto& window = dock.add_window("Main Window", bgui::dock_area::left);
    auto& window_assets = dock.add_window("Assets Window", bgui::dock_area::right);
    window.style.layout.padding = bgui::vec4i{0};

    m_window_context = &window.add_persistent<bgui::linear>(bgui::orientation::vertical);
    m_window_context->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);
    m_window_context->style.layout.padding = bgui::vec4i{0};
    m_window_context->style.layout.align = bgui::vec<2, bgui::alignment>{
        bgui::alignment::center,
        bgui::alignment::center
    };
    m_framebuffer_image = &m_window_context->add_persistent<bgui::image>();
    m_framebuffer_image->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);

    bgui::cascade_style();
    bgui::load_font_queue();
    m_initialized = true;
    debugging::emit(debug, "ui", "cpp-bgui inicializado");
    bgui::get_context().m_refresh_func = [this]() {
        bgui::glfw_update(bgui::get_context());
        bgui::load_font_queue();

        if (auto camera_component = m_camera.lock(); camera_component && m_window_context && m_framebuffer_image) {
            const auto source_size = camera_component->viewportFBO;
            const auto padding = m_window_context->computed_style.layout.padding;
            const int available_width = std::max(0, m_window_context->processed_width() - padding.x - padding.z);
            const int available_height = std::max(0, m_window_context->processed_height() - padding.y - padding.w);

            if (source_size.x > 0 && source_size.y > 0 && available_width > 0 && available_height > 0) {
                const float source_aspect = static_cast<float>(source_size.x) / source_size.y;
                const float target_aspect = static_cast<float>(available_width) / available_height;
                bgui::vec2 uv_min{0.f, 0.f};
                bgui::vec2 uv_max{1.f, 1.f};

                if (source_aspect > target_aspect) {
                    const float visible_width = target_aspect / source_aspect;
                    const float horizontal_crop = (1.f - visible_width) * 0.5f;
                    uv_min[0] = horizontal_crop;
                    uv_max[0] = 1.f - horizontal_crop;
                } else if (source_aspect < target_aspect) {
                    const float visible_height = source_aspect / target_aspect;
                    const float vertical_crop = (1.f - visible_height) * 0.5f;
                    uv_min[1] = vertical_crop;
                    uv_max[1] = 1.f - vertical_crop;
                }

                m_framebuffer_image->set_uv_region(uv_min, uv_max);

                if (available_width != m_framebuffer_display_width || available_height != m_framebuffer_display_height) {
                    m_framebuffer_image->set_size(static_cast<float>(available_width), static_cast<float>(available_height));
                    m_framebuffer_display_width = available_width;
                    m_framebuffer_display_height = available_height;
                }
            }
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
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

void ui_system::update(const std::shared_ptr<ecs>& reg)
{
    if (!reg || !m_framebuffer_image || !m_camera.expired())
        return;

    reg->cada<camera>([&](const uint32_t entity) {
        if (!m_camera.expired())
            return;

        auto camera_component = reg->get<camera>(entity);
        if (!camera_component)
            return;

        if (!camera_component->flag_fb)
            camera_component->createFB();

        m_framebuffer_image->set_external_texture(
            camera_component->framebuffer_texture(),
            bgui::vec2{
                static_cast<float>(camera_component->viewportFBO.x),
                static_cast<float>(camera_component->viewportFBO.y)
            }
        );
        m_camera = camera_component;
    });
}

ui_system::~ui_system()
{
    if (!m_initialized)
        return;

    bgui::shutdown_gl3();
    bgui::shutdown_freetype();
}
