#include "scene_view.hpp"

#include "camera_movement.hpp"
#include "entity_picking_system.hpp"
#include "guizmo_system.hpp"
#include "viewport_sync.hpp"

#include <bgui.hpp>
#include <elem/window.hpp>
#include <lay/dock.hpp>
#include <lay/linear.hpp>

#include <stdexcept>

#include "viewport_input_state.hpp"

namespace editor::ui {

scene_view::scene_view(
    commons::window& window,
    std::shared_ptr<commons::camera> camera,
    std::shared_ptr<editor_context> context
)
    : m_window(window),
      m_camera(camera),
      m_context(std::move(context)),
      m_input_state(std::make_shared<viewport_input_state>()) {
    if (!m_camera)
        throw std::invalid_argument("scene_view requires a camera");
}

void scene_view::build(bgui::dock& dock) {
    if (!m_camera->flag_fb)
        m_camera->createFB();

    auto& scene_window = dock.add_window("Scene", bgui::dock_area::center, 0.3f);
    scene_window.add_class("editor-panel");

    auto& scene_context = scene_window.add<bgui::linear>(
        bgui::orientation::vertical
    );
    scene_context.add_class("editor-surface");
    scene_context.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::match_parent
    );

    scene_context.style.layout.padding = bgui::vec4i{0};

    m_image = &scene_context.add<bgui::image>();
    m_image->add_class("editor-viewport-surface");

    m_image->style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::match_parent
    );

    m_image->receives_input(true);

    m_image->set_external_texture(
        m_camera->framebuffer_texture(),
        bgui::vec2{
            static_cast<float>(m_camera->viewportFBO.x),
            static_cast<float>(m_camera->viewportFBO.y)
        }
    );

    m_window.add<viewport_sync_system>(
        m_camera,
        *m_image
    );

    m_window.add<camera_movement_system>(
        m_camera,
        *m_image,
        m_input_state
    );

    scene_window.add<guizmo_system, bgui::layer::overlay>(
        m_camera,
        *m_image,
        m_context,
        m_input_state
    );

    m_window.add<entity_picking_system>(
        m_camera,
        *m_image,
        m_context,
        m_input_state
    );
}

void scene_view::update() {
}

bgui::image& scene_view::image() {
    if (!m_image)
        throw std::logic_error("scene_view has not been built");

    return *m_image;
}

std::shared_ptr<viewport_input_state> scene_view::input_state() const noexcept {
    return m_input_state;
}

}