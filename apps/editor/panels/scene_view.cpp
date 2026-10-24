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
#include <utility>

#include "viewport_input_state.hpp"

namespace editor::ui {

struct scene_view_state {
    bgui::image* image{nullptr};
};

namespace {

class scene_view_surface final : public bgui::linear {
public:
    explicit scene_view_surface(std::shared_ptr<scene_view_state> state)
        : bgui::linear(bgui::orientation::vertical),
          m_state(std::move(state)) {
    }

    ~scene_view_surface() override {
        m_state->image = nullptr;
    }

private:
    std::shared_ptr<scene_view_state> m_state;
};

class scene_view_runtime final : public bgui::element {
public:
    scene_view_runtime(
        std::shared_ptr<commons::ecs> registry,
        std::shared_ptr<commons::camera> camera,
        std::shared_ptr<editor_context> context,
        std::shared_ptr<viewport_input_state> input_state,
        const std::shared_ptr<scene_view_state>& state
    )
        : m_registry(std::move(registry)),
          m_viewport_sync(camera, *state->image),
          m_camera_movement(camera, *state->image, input_state),
          m_entity_picking(
              std::move(camera),
              *state->image,
              std::move(context),
              std::move(input_state)
          ) {
        set_flex(false);
        receives_input(false);
        style.visual.visible = false;
        m_camera_movement.setup(m_registry);
        m_entity_picking.setup(m_registry);
    }

    void on_update() override {
        m_viewport_sync.update(m_registry);
        m_camera_movement.update(m_registry);
        m_entity_picking.update(m_registry);
    }

private:
    std::shared_ptr<commons::ecs> m_registry;
    viewport_sync_system m_viewport_sync;
    camera_movement_system m_camera_movement;
    entity_picking_system m_entity_picking;
};

}

scene_view::scene_view(
    commons::window& window,
    std::shared_ptr<commons::camera> camera,
    std::shared_ptr<editor_context> context
)
    : m_window(window),
      m_camera(camera),
      m_context(std::move(context)),
      m_input_state(std::make_shared<viewport_input_state>()),
      m_state(std::make_shared<scene_view_state>()) {
    if (!m_camera)
        throw std::invalid_argument("scene_view requires a camera");
}

void scene_view::build(bgui::dock& dock) {
    if (!m_camera->flag_fb)
        m_camera->createFB();

    auto& scene_window = dock.add_window("Scene", bgui::dock_area::center, 0.3f);
    scene_window.add_class("editor-panel");

    auto& scene_context = scene_window.add<scene_view_surface>(m_state);
    scene_context.add_class("editor-surface");
    scene_context.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::match_parent
    );

    scene_context.style.layout.padding = bgui::vec4i{0};

    auto& image = scene_context.add<bgui::image>();
    m_state->image = &image;
    image.add_class("editor-viewport-surface");

    image.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::match_parent
    );

    image.receives_input(true);

    image.set_external_texture(
        m_camera->framebuffer_texture(),
        bgui::vec2{
            static_cast<float>(m_camera->viewportFBO.x),
            static_cast<float>(m_camera->viewportFBO.y)
        }
    );

    scene_window.add<scene_view_runtime, bgui::layer::overlay>(
        m_window.get_ecs(),
        m_camera,
        m_context,
        m_input_state,
        m_state
    );

    scene_window.add<guizmo_system, bgui::layer::overlay>(
        m_camera,
        image,
        m_context,
        m_input_state
    );
}

void scene_view::update() {
}

bgui::image& scene_view::image() {
    if (!m_state || !m_state->image)
        throw std::logic_error("scene_view has not been built or is closed");

    return *m_state->image;
}

bool scene_view::is_open() const noexcept {
    return m_state && m_state->image;
}

std::shared_ptr<viewport_input_state> scene_view::input_state() const noexcept {
    return m_input_state;
}

}