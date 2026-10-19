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
#include <utility>
#include <vector>

namespace editor::ui {

class guizmo_system final : public bgui::element {
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

    void get_requires(bgui::draw_data* data) override {
        if (!m_camera || !m_image || !m_context || !m_context->registry) {
            bgui::element::get_requires(data);
            return;
        }

        if (m_context->is_playing) {
            if (m_input_state)
                m_input_state->gizmo_active = false;
            m_last_mouse_position = bgui::get_mouse_position();
            m_left_was_down = bgui::get_pressed(
                bgui::input_key::mouse_left
            );
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

        std::shared_ptr<commons::transform> selected_transform;
        if (m_context && m_context->selected_entity) {
            selected_transform = m_context->registry->get<commons::transform>(
                *m_context->selected_entity
            );
        }

        if (selected_transform) {
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