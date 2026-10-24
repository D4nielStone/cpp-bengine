#pragma once

#include "components/camera.hpp"
#include "components/renderer.hpp"
#include "components/transform.hpp"
#include "editor_context.hpp"
#include "systems/system.hpp"
#include "viewport_input_state.hpp"

#include <elem/image.hpp>

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace editor::ui {

class entity_picking_system final : public commons::system {
public:
    entity_picking_system(
        std::shared_ptr<commons::camera> camera,
        bgui::image& image,
        std::shared_ptr<editor_context> context,
        std::shared_ptr<viewport_input_state> input_state
    )
        : m_camera(std::move(camera)),
          m_image(&image),
          m_context(std::move(context)),
          m_input_state(std::move(input_state)) {}

    void setup(const std::shared_ptr<commons::ecs>&) override {
        m_left_was_down = bgui::get_pressed(bgui::input_key::mouse_left);
    }

    void update(const std::shared_ptr<commons::ecs>& registry) override {
        if (!m_camera || !m_image || !m_context || !registry)
            return;

        const bool left_down = bgui::get_pressed(bgui::input_key::mouse_left);
        const bool just_pressed = left_down && !m_left_was_down;
        m_left_was_down = left_down;

        std::optional<bgui::vec2i> pick_position;
        if (m_pending_pick) {
            if (m_input_state && m_input_state->gizmo_active) {
                m_pending_pick.reset();
            } else {
                pick_position = m_pending_pick;
                m_pending_pick.reset();
            }
        }

        if (just_pressed &&
            !(m_input_state && m_input_state->gizmo_active)) {
            m_pending_pick = bgui::get_mouse_position();
        }

        if (!pick_position)
            return;

        const auto mouse = *pick_position;
        const int x = m_image->processed_x();
        const int y = m_image->processed_y();
        const int width = m_image->processed_width();
        const int height = m_image->processed_height();

        if (width <= 0 || height <= 0 ||
            mouse.x < x || mouse.x >= x + width ||
            mouse.y < y || mouse.y >= y + height) {
            return;
        }

        m_camera->getViewMatrix();
        m_camera->obtProjectionMatrix();
        const auto ray = m_camera->point_to_ray({
            static_cast<float>(mouse.x - x),
            static_cast<float>(mouse.y - y)
        });
        const glm::vec3 origin{ray.origem.x, ray.origem.y, ray.origem.z};
        const glm::vec3 direction{
            ray.direction.x,
            ray.direction.y,
            ray.direction.z
        };

        std::optional<editor_context::entity_id> hit_entity;
        float closest_distance = std::numeric_limits<float>::max();

        registry->cada<commons::renderer, commons::transform>(
            [&](const std::uint32_t entity) {
                if (m_context->hidden_entities.contains(entity) ||
                    m_context->locked_entities.contains(entity)) {
                    return;
                }

                const auto renderer = registry->get<commons::renderer>(entity);
                const auto transform = registry->get<commons::transform>(entity);
                if (!renderer || !renderer->visible || !renderer->m_modelo || !transform)
                    return;

                const auto model_matrix = transform->get_model_matrix();
                for (const auto& mesh : renderer->m_modelo->meshes) {
                    const auto vertices = mesh.getVertices();
                    const auto indices = mesh.getIndices();

                    for (std::size_t index = 0; index + 2 < indices.size(); index += 3) {
                        const auto first_index = indices[index];
                        const auto second_index = indices[index + 1];
                        const auto third_index = indices[index + 2];

                        if (first_index >= vertices.size() ||
                            second_index >= vertices.size() ||
                            third_index >= vertices.size()) {
                            continue;
                        }

                        const auto to_world = [&model_matrix](const bgui::vec3& position) {
                            const glm::vec4 world = model_matrix *
                                glm::vec4(position.x, position.y, position.z, 1.0f);
                            return glm::vec3(world) / world.w;
                        };

                        const auto distance = intersect_triangle(
                            origin,
                            direction,
                            to_world(vertices[first_index].position),
                            to_world(vertices[second_index].position),
                            to_world(vertices[third_index].position)
                        );

                        if (distance && *distance < closest_distance) {
                            closest_distance = *distance;
                            hit_entity = entity;
                        }
                    }
                }
            }
        );

        m_context->selected_entity = hit_entity;
    }

private:
    static std::optional<float> intersect_triangle(
        const glm::vec3& origin,
        const glm::vec3& direction,
        const glm::vec3& first,
        const glm::vec3& second,
        const glm::vec3& third
    ) {
        constexpr float epsilon = 0.000001f;
        const glm::vec3 edge_one = second - first;
        const glm::vec3 edge_two = third - first;
        const glm::vec3 cross_direction = glm::cross(direction, edge_two);
        const float determinant = glm::dot(edge_one, cross_direction);

        if (std::abs(determinant) < epsilon)
            return std::nullopt;

        const float inverse_determinant = 1.0f / determinant;
        const glm::vec3 from_first = origin - first;
        const float barycentric_u =
            glm::dot(from_first, cross_direction) * inverse_determinant;
        if (barycentric_u < 0.0f || barycentric_u > 1.0f)
            return std::nullopt;

        const glm::vec3 cross_from_first = glm::cross(from_first, edge_one);
        const float barycentric_v =
            glm::dot(direction, cross_from_first) * inverse_determinant;
        if (barycentric_v < 0.0f ||
            barycentric_u + barycentric_v > 1.0f) {
            return std::nullopt;
        }

        const float distance =
            glm::dot(edge_two, cross_from_first) * inverse_determinant;
        if (distance <= epsilon)
            return std::nullopt;

        return distance;
    }

    std::shared_ptr<commons::camera> m_camera;
    bgui::image* m_image{nullptr};
    std::shared_ptr<editor_context> m_context;
    std::shared_ptr<viewport_input_state> m_input_state;
    std::optional<bgui::vec2i> m_pending_pick;
    bool m_left_was_down{false};
};

}
