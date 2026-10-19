
#pragma once

#include "components/camera.hpp"
#include "components/transform.hpp"
#include "api/mat.hpp"
#include "systems/system.hpp"
#include "utils/vec.hpp"
#include "viewport_input_state.hpp"
#include <cmath>
#include <elem/image.hpp>
#include <os/os.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace editor::ui {

class camera_movement_system final : public commons::system {
private:
    std::shared_ptr<commons::camera> m_camera;
    std::shared_ptr<viewport_input_state> m_input_state;
    std::shared_ptr<commons::transform> m_orbit_target;

    bgui::image* m_image{nullptr};

    bgui::vec2i m_last_mouse_position{0, 0};

    bgui::vec3 m_orbit_center{0.0f, 0.0f, 0.0f};

    float m_movement_speed{5.0f};
    float m_sprint_multiplier{3.0f};
    float m_mouse_sensitivity{0.15f};
    float m_zoom_sensitivity{0.8f};
    float m_min_orbit_distance{0.25f};
    float m_max_orbit_distance{1000.0f};
    float m_smoothing_speed{12.0f};

    float m_orbit_distance{10.0f};
    float m_yaw{0.0f};
    float m_pitch{0.0f};

    float m_previous_time{0.0f};

    bool m_mouse_initialized{false};
    bool m_orbit_initialized{false};

    static constexpr float radians_to_degrees =
        57.29577951308232f;

    bgui::vec3 get_forward() const {
        const float yaw = glm::radians(m_yaw);
        const float pitch = glm::radians(m_pitch);
        const float cos_pitch = std::cos(pitch);

        return {
            cos_pitch * std::cos(yaw),
            std::sin(pitch),
            cos_pitch * std::sin(yaw)
        };
    }

    bgui::vec3 get_right() const {
        const float yaw = glm::radians(m_yaw);

        return {
            -std::sin(yaw),
            0.0f,
            std::cos(yaw)
        };
    }

    void initialize_orbit(
        const std::shared_ptr<commons::transform>& camera_transform
    ) {
        const auto camera_position =
            camera_transform->get_position();

        const auto camera_rotation =
            camera_transform->get_rotation();

        m_yaw = camera_rotation.y;
        m_pitch = std::clamp(
            camera_rotation.x,
            -89.0f,
            89.0f
        );

        if (m_orbit_target) {
            m_orbit_center = m_orbit_target->get_position();
        } else {
            const auto forward = get_forward();

            m_orbit_center = camera_position + forward * 10.0f;
        }

        const auto offset = camera_position - m_orbit_center;
        const float distance = offset.length();

        if (distance > 0.0001f) {
            m_orbit_distance = std::clamp(
                distance,
                m_min_orbit_distance,
                m_max_orbit_distance
            );
        } else {
            m_orbit_distance = 10.0f;
        }

        m_orbit_initialized = true;
    }

    void update_orbit_transform(
        const std::shared_ptr<commons::transform>& camera_transform,
        const float delta_time
    ) {
        const auto forward = get_forward();

        const auto camera_position =
            m_orbit_center - forward * m_orbit_distance;

        const float interpolation = 1.0f - std::exp(
            -m_smoothing_speed * delta_time
        );
        const auto camera_rotation = bgui::vec3{
            m_pitch,
            m_yaw,
            0.0f
        };

        camera_transform->set_position(commons::lerp(
            camera_transform->get_position(),
            camera_position,
            interpolation
        ));
        camera_transform->set_rotation(commons::lerp(
            camera_transform->get_rotation(),
            camera_rotation,
            interpolation
        ));
    }

public:
    camera_movement_system(
        std::shared_ptr<commons::camera> camera_component,
        bgui::image& image,
        std::shared_ptr<viewport_input_state> input_state
    )
        : m_camera(std::move(camera_component)),
          m_input_state(std::move(input_state)),
          m_image(&image) {
    }

    void setup(const std::shared_ptr<commons::ecs>&) override {
        m_last_mouse_position = bgui::get_mouse_position();
        m_mouse_initialized = true;
        m_previous_time = bgui::get_time();
        m_orbit_initialized = false;
    }

    void set_orbit_target(
        std::shared_ptr<commons::transform> target
    ) {
        m_orbit_target = std::move(target);
        m_orbit_initialized = false;
    }

    void clear_orbit_target() {
        m_orbit_target.reset();
        m_orbit_initialized = false;
    }

    void set_orbit_center(const bgui::vec3& center) {
        m_orbit_center = center;
        m_orbit_initialized = true;
    }

    void update(
        const std::shared_ptr<commons::ecs>& registry
    ) override {
        if (!m_camera || !m_image || !registry)
            return;

        auto camera_transform = registry->get<commons::transform>(
            m_camera->my_object
        );

        if (!camera_transform)
            return;

        const bgui::vec2i mouse = bgui::get_mouse_position();

        if (!m_mouse_initialized) {
            m_last_mouse_position = mouse;
            m_mouse_initialized = true;
        }

        const bgui::vec2i mouse_delta =
            mouse - m_last_mouse_position;

        m_last_mouse_position = mouse;

        const float current_time = bgui::get_time();

        const float delta_time = std::clamp(
            current_time - m_previous_time,
            0.0f,
            0.05f
        );

        m_previous_time = current_time;

        const int x = m_image->processed_x();
        const int y = m_image->processed_y();
        const int width = m_image->processed_width();
        const int height = m_image->processed_height();

        if (width <= 0 || height <= 0)
            return;

        const bool mouse_over_viewport =
            mouse.x >= x &&
            mouse.y >= y &&
            mouse.x < x + width &&
            mouse.y < y + height;

        if (!mouse_over_viewport)
            return;

        if (!m_orbit_initialized)
            initialize_orbit(camera_transform);

        const bool gizmo_active =
            m_input_state && m_input_state->gizmo_active;

        if (m_orbit_target)
            m_orbit_center = m_orbit_target->get_position();

        if (!gizmo_active) {
            const bool rotate_camera =
                bgui::get_pressed(bgui::input_key::mouse_left);

            if (rotate_camera) {
                m_yaw += static_cast<float>(mouse_delta.x)
                    * m_mouse_sensitivity;

                m_pitch -= static_cast<float>(mouse_delta.y)
                    * m_mouse_sensitivity;

                m_pitch = std::clamp(
                    m_pitch,
                    -89.0f,
                    89.0f
                );
            }

            const float scroll_delta =
                bgui::get_context().m_scroll_delta_y;

            if (scroll_delta != 0.0f) {
                m_orbit_distance = std::clamp(
                    m_orbit_distance
                        - scroll_delta * m_zoom_sensitivity,
                    m_min_orbit_distance,
                    m_max_orbit_distance
                );
            }

            const auto forward = get_forward();
            const auto right = get_right();

            bgui::vec3 movement{0.0f, 0.0f, 0.0f};

            if (bgui::get_pressed(bgui::input_key::w))
                movement += forward;

            if (bgui::get_pressed(bgui::input_key::s))
                movement -= forward;

            if (bgui::get_pressed(bgui::input_key::d))
                movement += right;

            if (bgui::get_pressed(bgui::input_key::a))
                movement += right * -1.0f;

            if (bgui::get_pressed(bgui::input_key::e))
                movement.y += 1.0f;

            if (bgui::get_pressed(bgui::input_key::q))
                movement.y -= 1.0f;

            const float movement_length = movement.length();

            if (movement_length > 0.0f) {
                movement /= movement_length;

                const float speed = m_movement_speed *
                    (bgui::get_pressed(
                        bgui::input_key::left_shift
                    ) ? m_sprint_multiplier : 1.0f);

                const auto displacement =
                    movement * speed * delta_time;

                m_orbit_center += displacement;

                if (m_orbit_target) {
                    m_orbit_target->set_position(
                        m_orbit_target->get_position()
                            + displacement
                    );
                }
            }
        }

        update_orbit_transform(camera_transform, delta_time);
    }

    void set_movement_speed(float speed) {
        m_movement_speed = std::max(0.0f, speed);
    }

    void set_sprint_multiplier(float multiplier) {
        m_sprint_multiplier = std::max(1.0f, multiplier);
    }

    void set_mouse_sensitivity(float sensitivity) {
        m_mouse_sensitivity = std::max(0.0f, sensitivity);
    }

    void set_zoom_sensitivity(float sensitivity) {
        m_zoom_sensitivity = std::max(0.0f, sensitivity);
    }

    void set_smoothing_speed(float speed) {
        m_smoothing_speed = std::max(0.0f, speed);
    }

    void set_orbit_distance_limits(float min_distance, float max_distance) {
        m_min_orbit_distance = std::max(0.01f, min_distance);
        m_max_orbit_distance = std::max(
            m_min_orbit_distance,
            max_distance
        );

        m_orbit_distance = std::clamp(
            m_orbit_distance,
            m_min_orbit_distance,
            m_max_orbit_distance
        );
    }

    void set_orbit_distance(float distance) {
        m_orbit_distance = std::clamp(
            distance,
            m_min_orbit_distance,
            m_max_orbit_distance
        );
    }
};

}