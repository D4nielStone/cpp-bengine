#define GLFW_INCLUDE_NONE
#include <glad.h>

#include "scene_presenter.hpp"

#include "camera_movement.hpp"
#include "camera_preview_state.hpp"
#include "camera_viewport.hpp"
#include "direction_gizmo.hpp"
#include "editor_commands.hpp"
#include "entity_picking_system.hpp"
#include "guizmo_system.hpp"
#include "os/window.hpp"
#include "scene_navigation.hpp"
#include "ui/hud_view.hpp"
#include "viewport_sync.hpp"

#include <stdexcept>

namespace editor::ui {
namespace {

class scene_view_runtime final : public bgui::element {
public:
    scene_view_runtime(
        std::shared_ptr<commons::ecs> registry,
        std::shared_ptr<commons::camera> camera,
        std::shared_ptr<editor_context> context,
        std::shared_ptr<viewport_input_state> input_state,
        bgui::image& image
    )
        : m_registry(std::move(registry)),
          m_viewport(camera),
          m_viewport_sync(m_viewport, image),
          m_camera_movement(camera, image, input_state),
          m_entity_picking(
              camera,
              image,
              context,
              std::move(input_state)
          ),
          m_context(std::move(context)), m_image(image), m_camera(std::move(camera)) {
        set_flex(false);
        // This overlay spans the scene window. It acts as the scene viewport's
        // input hit target so hit testing can consume input without hiding the
        // owning window from viewport_input_state.
        receives_input(true);
        style.visual.visible = false;
        m_camera_movement.setup(m_registry);
        m_entity_picking.setup(m_registry);
    }

    void on_update() override {
        m_viewport_sync.update(m_registry);
        const bool toggle = bgui::get_pressed(bgui::input_key::o);
        if (toggle && !m_toggle_down && mouse_over_viewport(&m_image) &&
            !viewport_input_blocked_by_modal() &&
            !dynamic_cast<bgui::inputbox*>(bgui::get_keyboard_focus()))
            editor_commands(m_context).execute(editor_command::toggle_orbital);
        m_toggle_down = toggle;
        const auto& preferences = m_context->preferences;
        m_camera_movement.set_movement_speed(preferences.movement_speed);
        m_camera_movement.set_sprint_multiplier(preferences.sprint_multiplier);
        m_camera_movement.set_mouse_sensitivity(preferences.mouse_sensitivity);
        m_camera_movement.set_zoom_sensitivity(preferences.zoom_sensitivity);
        m_camera_movement.set_smoothing_speed(preferences.smoothing_speed);
        auto selected = m_context->selected_entity &&
            m_context->selected_entity != m_context->editor_camera_entity
            ? m_context->registry->get<commons::transform>(*m_context->selected_entity) : nullptr;
        if (m_context->orbital_view && selected) {
            if (!m_camera_movement.is_orbital() || selected != m_selected_orbit) {
                auto renderer = m_context->registry->get<commons::renderer>(selected->my_object);
                m_camera_movement.focus_orbit_target(selected,
                    selected_orbit_distance(*selected, renderer.get(), m_camera->config.fov));
            }
            m_selected_orbit = selected;
        } else {
            if (m_selected_orbit) m_camera_movement.set_orbital(false);
            m_camera_movement.set_orbital(m_context->orbital_view);
            m_selected_orbit.reset();
        }
        m_camera_movement.update(m_registry);
        m_entity_picking.update(m_registry);
    }

private:
    std::shared_ptr<commons::ecs> m_registry;
    bool m_toggle_down{false};
    camera_viewport m_viewport;
    viewport_sync_system m_viewport_sync;
    camera_movement_system m_camera_movement;
    entity_picking_system m_entity_picking;
    std::shared_ptr<editor_context> m_context;
    bgui::image& m_image;
    std::shared_ptr<commons::camera> m_camera;
    std::shared_ptr<commons::transform> m_selected_orbit;
};

}

scene_presenter::scene_presenter(commons::window& window, std::shared_ptr<commons::camera> camera,
    std::shared_ptr<editor_context> context)
    : m_window(window), m_camera(std::move(camera)), m_context(std::move(context)),
      m_input_state(std::make_shared<viewport_input_state>()),
      m_camera_preview(std::make_shared<camera_preview_state>()) {
    if (!m_camera) throw std::invalid_argument("scene_view requires a camera");
}

viewport_frame scene_presenter::initialize_viewport() {
    camera_viewport viewport(m_camera);
    viewport.initialize();
    return viewport.frame();
}

void scene_presenter::attach_overlays(bgui::window& window, bgui::image& image) {
    window.add<scene_view_runtime, bgui::layer::overlay>(
        m_window.get_ecs(), m_camera, m_context, m_input_state, image);
    window.add<commons::hud_view, bgui::layer::overlay>(
        m_window.get_ecs(), m_camera, image,
        [weak = std::weak_ptr<editor_context>(m_context)] { auto c = weak.lock(); return c && c->is_playing; });
    window.add<guizmo_system, bgui::layer::overlay>(m_camera, image, m_context, m_input_state, m_camera_preview);
    window.add<direction_gizmo, bgui::layer::overlay>(m_camera, image);
}

std::optional<editor_api::entity_id> scene_presenter::pick(bgui::image& image, int x, int y) const {
    return entity_picking_system(m_camera, image, m_context, m_input_state).pick_at({x, y});
}

bool scene_presenter::needs_camera_render(std::uint32_t entity) const noexcept {
    return entity == m_camera->my_object ||
        (m_camera_preview->hovered && m_camera_preview->main_runtime_camera &&
         entity == m_camera_preview->main_runtime_camera->my_object);
}

std::optional<scene_presenter::preview_bounds> scene_presenter::preview_area(float scale) const {
    if (!m_camera_preview->hovered || !m_camera_preview->main_runtime_camera)
        return std::nullopt;
    const auto rect = m_camera_preview->viewport;
    return preview_bounds{rect.x, rect.y, rect.z, rect.w, direction_gizmo_rect(rect, scale).w};
}

std::optional<scene_presenter::preview> scene_presenter::preview_frame(int width, int height) const {
    const auto& camera = m_camera_preview->main_runtime_camera;
    if (!m_camera_preview->hovered || !camera) return std::nullopt;
    camera_viewport viewport(camera);
    viewport.request_size(width, height);
    const auto frame = viewport.frame();
    if (!frame.texture) return std::nullopt;
    return preview{frame, editor_api(m_context).entity_name(camera->my_object,
        [](auto id) { return "Camera " + std::to_string(id); })};
}

bool scene_presenter::orbital() const noexcept {
    return m_context->orbital_view;
}

void scene_presenter::toggle_orbital() {
    editor_commands(m_context).execute(editor_command::toggle_orbital);
}

}
