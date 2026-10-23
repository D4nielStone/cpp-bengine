#pragma once

#include "components/camera.hpp"
#include "components/transform.hpp"
#include "editor_context.hpp"
#include "viewport_input_state.hpp"
#include "guizmo/transform_gizmo.hpp"
#include "guizmo/grid_gizmo.hpp"

#include <elem/image.hpp>
#include <os/os.hpp>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace editor::ui {

class guizmo_system final : public bgui::element {
    struct transform_snapshot {
        std::shared_ptr<commons::transform> transform;
        bgui::vec3 position;
        bgui::vec3 rotation;
        bgui::vec3 scale;
    };

private:
    std::shared_ptr<commons::camera> m_camera;
    std::shared_ptr<editor_context> m_context;
    std::shared_ptr<viewport_input_state> m_input_state;
    bgui::image* m_image{nullptr};

    editor::transform_gizmo m_guizmo;
    editor::grid_gizmo m_grid_gizmo;

    bgui::vec2i m_last_mouse_position{0, 0};

    bool m_mouse_position_initialized{false};
    bool m_left_was_down{false};
    std::optional<transform_snapshot> m_drag_snapshot;

    static bool same_vector(const bgui::vec3& left, const bgui::vec3& right) {
        return left.x == right.x && left.y == right.y && left.z == right.z;
    }

    void finish_transform_action() {
        if (!m_drag_snapshot)
            return;

        auto snapshot = std::move(*m_drag_snapshot);
        m_drag_snapshot.reset();
        const auto position = snapshot.transform->get_position();
        const auto rotation = snapshot.transform->get_rotation();
        const auto scale = snapshot.transform->get_scale();
        if (same_vector(snapshot.position, position) &&
            same_vector(snapshot.rotation, rotation) &&
            same_vector(snapshot.scale, scale))
            return;

        const auto transform = snapshot.transform;
        const auto old_position = snapshot.position;
        const auto old_rotation = snapshot.rotation;
        const auto old_scale = snapshot.scale;
        m_context->record_action(
            [transform, old_position, old_rotation, old_scale]() {
                transform->set_position(old_position);
                transform->set_rotation(old_rotation);
                transform->set_scale(old_scale);
            },
            [transform, position, rotation, scale]() {
                transform->set_position(position);
                transform->set_rotation(rotation);
                transform->set_scale(scale);
            }
        );
    }

public:
    guizmo_system(
        std::shared_ptr<commons::camera> camera_component,
        bgui::image& image,
        std::shared_ptr<editor_context> context,
        std::shared_ptr<viewport_input_state> input_state
    )
        : m_camera(std::move(camera_component)),
          m_context(std::move(context)),
          m_input_state(std::move(input_state)),
          m_image(&image) {
        receives_input(false);
        set_flex(false);
        style.visual.visible = false;
    }

    void on_update() override {
        if (m_image) {
            const auto rect = m_image->processed_rect();
            set_final_rect(rect.x, rect.y, rect.z, rect.w);
        }
    }

    bool supports_render_cache() const override { return false; }

    void get_requires(bgui::draw_data* data) override {
        if (!m_camera || !m_image || !m_context || !m_context->registry) {
            bgui::element::get_requires(data);
            return;
        }

        const bool left_down = bgui::get_pressed(
            bgui::input_key::mouse_left
        );

        const bgui::vec2i mouse_position =
            bgui::get_mouse_position();

        if (!m_mouse_position_initialized) {
            m_last_mouse_position = mouse_position;
            m_left_was_down = left_down;
            m_mouse_position_initialized = true;
            if (m_input_state)
                m_input_state->gizmo_active = false;
        }

        const bgui::vec2i mouse_delta{
            mouse_position.x - m_last_mouse_position.x,
            mouse_position.y - m_last_mouse_position.y
        };

        const bool left_just_pressed =
            left_down && !m_left_was_down;

        const int x = m_image->processed_x();
        const int y = m_image->processed_y();
        const int width = m_image->processed_width();
        const int height = m_image->processed_height();

        if (width <= 0 || height <= 0) {
            m_last_mouse_position = mouse_position;
            m_left_was_down = left_down;

            if (m_input_state)
                m_input_state->gizmo_active = false;

            bgui::element::get_requires(data);
            return;
        }

        const bgui::vec4i viewport{
            x,
            y,
            width,
            height
        };

        const bool mouse_over_view =
            mouse_position.x >= x &&
            mouse_position.x < x + width &&
            mouse_position.y >= y &&
            mouse_position.y < y + height;

        const std::vector<bgui::vec4i> visible_regions{
            viewport
        };

        m_grid_gizmo.draw(
            *m_camera,
            viewport
        );

        if (m_context->show_camera_frustum && m_context->selected_entity) {
            const auto frustum_camera = m_context->registry->get<commons::camera>(
                *m_context->selected_entity
            );
            if (frustum_camera && frustum_camera != m_camera)
                m_grid_gizmo.draw_camera_frustum(
                    *m_camera,
                    *frustum_camera,
                    viewport
                );
        }

        std::shared_ptr<commons::transform> selected_transform;
        if (m_context && m_context->selected_entity) {
            selected_transform = m_context->registry->get<commons::transform>(
                *m_context->selected_entity
            );
        }

        if (selected_transform) {
            const bool was_dragging = m_guizmo.is_dragging();
            m_guizmo.update(
                *m_camera,
                selected_transform.get(),
                viewport,
                visible_regions,
                mouse_position,
                mouse_delta,
                mouse_over_view,
                left_just_pressed,
                left_down
            );
            const bool is_dragging = m_guizmo.is_dragging();
            if (!was_dragging && is_dragging && m_context->selected_entity) {
                m_drag_snapshot = transform_snapshot{
                    selected_transform,
                    selected_transform->get_position(),
                    selected_transform->get_rotation(),
                    selected_transform->get_scale()
                };
            } else if (was_dragging && !is_dragging) {
                finish_transform_action();
            }
        }

        if (m_input_state) {
            m_input_state->gizmo_active =
                selected_transform && m_guizmo.is_dragging();
        }

        m_last_mouse_position = mouse_position;
        m_left_was_down = left_down;
        bgui::element::get_requires(data);
    }
};

}