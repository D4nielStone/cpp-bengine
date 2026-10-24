#include "components/camera.hpp"
#include "components/renderer.hpp"
#include "runtime_tools_view.hpp"

#include <lay/dock.hpp>
#include <lay/linear.hpp>

#include <iomanip>
#include <functional>
#include <sstream>
#include <stdexcept>

namespace editor::ui {
namespace {

class runtime_tools_layout final : public bgui::linear {
public:
    runtime_tools_layout()
        : bgui::linear(bgui::orientation::vertical) {}

    std::function<void()> before_update;

    void on_update() override {
        if (before_update)
            before_update();
        bgui::linear::on_update();
    }
};

}

runtime_tools_view::runtime_tools_view(
    commons::window& window,
    std::shared_ptr<editor_context> context,
    std::shared_ptr<commons::render_system> render_system
)
    : m_window(window),
      m_context(std::move(context)),
      m_render_system(std::move(render_system)) {
    if (!m_context || !m_context->registry || !m_render_system)
        throw std::invalid_argument(
            "runtime_tools_view requires an editor context and render system"
        );
}

void runtime_tools_view::build(bgui::dock& dock) {
    auto& runtime_window = dock.add_window(
        "Editor Diagnostics",
        bgui::dock_area::right,
        0.24f
    );
    runtime_window.add_class("editor-panel");

    auto& content = runtime_window.add<runtime_tools_layout>();
    content.before_update = [this]() { update(); };
    content.add_class("editor-surface");
    content.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::match_parent
    );

    m_runtime_status = &content.add<bgui::text>("Game window: stopped", 0.31f);
    m_frame_profile = &content.add<bgui::text>("Frame: 0.00 ms | FPS: 0", 0.29f);
    m_render_profile = &content.add<bgui::text>("Render: 0.00 ms", 0.29f);
    m_scene_debug = &content.add<bgui::text>(
        "Scene: 0 entities | 0 renderers | 0 cameras",
        0.29f
    );
}

void runtime_tools_view::update() {
    if (!m_runtime_status || !m_frame_profile ||
        !m_render_profile || !m_scene_debug) {
        throw std::logic_error("runtime_tools_view has not been built");
    }

    const double frame_time = m_window.delta_time();
    m_fps_elapsed += frame_time;
    ++m_frame_count;
    if (m_fps_elapsed >= 1.0) {
        m_fps = static_cast<double>(m_frame_count) / m_fps_elapsed;
        m_fps_elapsed = 0.0;
        m_frame_count = 0;
    }
    std::ostringstream frame;
    frame << std::fixed << std::setprecision(2)
          << "Frame: " << frame_time * 1000.0 << " ms | FPS: "
          << std::setprecision(0) << m_fps;
    m_frame_profile->set_buffer(frame.str());

    std::ostringstream render;
    render << std::fixed << std::setprecision(2)
           << "CPU render: " << m_render_system->last_render_time_ms()
           << " ms | Cameras: " << m_render_system->last_camera_count()
           << " | Draws: "
           << m_render_system->last_rendered_entity_count();
    m_render_profile->set_buffer(render.str());
    m_runtime_status->set_buffer(
        m_context->is_playing ? "Game window: running" : "Game window: stopped"
    );

    std::size_t entity_count = 0;
    std::size_t renderer_count = 0;
    std::size_t visible_renderer_count = 0;
    std::size_t camera_count = 0;
    for (const auto& [id, components] : m_context->registry->entities) {
        (void)components;
        if (m_context->editor_camera_entity == id)
            continue;

        ++entity_count;
        if (const auto renderer = m_context->registry->get<commons::renderer>(id)) {
            ++renderer_count;
            if (renderer->visible)
                ++visible_renderer_count;
        }
        if (m_context->registry->get<commons::camera>(id))
            ++camera_count;
    }

    m_scene_debug->set_buffer(
        "Scene: " + std::to_string(entity_count) + " entities | " +
        std::to_string(visible_renderer_count) + "/" +
        std::to_string(renderer_count) + " visible renderers | " +
        std::to_string(camera_count) + " cameras"
    );
}

}
