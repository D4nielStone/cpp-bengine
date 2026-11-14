#include "systems/rig_entities.hpp"
#pragma once

#include "components/camera.hpp"
#include "components/renderer.hpp"
#include "components/transform.hpp"
#include "editor_context.hpp"
#include "systems/system.hpp"
#include "viewport_input_state.hpp"

#include <elem/image.hpp>

#include <glm/glm.hpp>

#include <algorithm>
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

    void set_rectangle_selection_enabled(bool enabled) { m_rectangle_enabled = enabled; }

    void setup(const std::shared_ptr<commons::ecs>&) override {
        m_left_was_down = bgui::get_pressed(bgui::input_key::mouse_left);
    }

    void update(const std::shared_ptr<commons::ecs>& registry) override {
        if (!m_camera || !m_image || !m_context || !registry)
            return;

        const bool left_down = bgui::get_pressed(bgui::input_key::mouse_left);
        const bool just_pressed = left_down && !m_left_was_down;
        const bool released = !left_down && m_left_was_down;
        m_left_was_down = left_down;
        if (viewport_input_blocked_by_modal() || bgui::get_pressed(bgui::input_key::mouse_right) ||
            (m_input_state && m_input_state->gizmo_active) ||
            (m_rectangle_enabled && ((viewport_camera_modifier() && m_box_drag) ||
                bgui::get_pressed(bgui::input_key::escape) || m_context->is_playing))) {
            m_pending_pick.reset();
            m_box_drag = false;
            return;
        }
        if (just_pressed && mouse_over_viewport(m_image)) {
            m_pending_pick = bgui::get_mouse_position();
            m_extend = bgui::get_pressed(bgui::input_key::left_shift) ||
                bgui::get_pressed(bgui::input_key::right_shift) ||
                bgui::get_pressed(bgui::input_key::left_control) ||
                bgui::get_pressed(bgui::input_key::right_control);
            m_box_drag = false;
        }
        if (m_pending_pick) {
            const auto delta = bgui::get_mouse_position() - *m_pending_pick;
            const float threshold = 4.f * bgui::get_global_scale();
            if (float(delta.x) * delta.x + float(delta.y) * delta.y > threshold * threshold) {
                if (m_rectangle_enabled && !viewport_camera_modifier()) m_box_drag = true;
                else m_pending_pick.reset();
            }
        }
        if (released && m_pending_pick) {
            if (m_box_drag)
                select_rectangle(*m_pending_pick, bgui::get_mouse_position(), m_extend);
            else if (mouse_over_viewport(m_image))
                m_context->select_entity(pick_at(*m_pending_pick), m_extend);
            m_pending_pick.reset();
            m_box_drag = false;
        }
    }

    void draw_selection_rectangle() const {
        if (!m_pending_pick || !m_box_drag || !m_image ||
            viewport_input_blocked_by_modal() || (m_input_state && m_input_state->gizmo_active)) return;
        const auto start = *m_pending_pick, end = bgui::get_mouse_position();
        auto& draw = bgui::get_draw_list();
        const auto color = draw.get_color();
        const auto clip = draw.get_clip_rect();
        const bool clipped = draw.has_clip_rect();
        const auto rect = m_image->processed_rect();
        draw.set_clip_rect(clipped ? bgui::intersect_rect(clip, rect) : rect);
        const bgui::vec2 low{float(std::min(start.x, end.x)), float(std::min(start.y, end.y))};
        const bgui::vec2 high{float(std::max(start.x, end.x)), float(std::max(start.y, end.y))};
        draw.set_color({.50f, .50f, .50f, .18f});
        draw.add_rect_filled(low, high);
        draw.set_color({.65f, .65f, .65f, 1.f});
        draw.add_rect(low, high);
        draw.set_color(color);
        if (clipped) draw.set_clip_rect(clip);
        else draw.clear_clip_rect();
    }

    void select_rectangle(bgui::vec2i start, bgui::vec2i end, bool extend) {
        const auto registry = m_context ? m_context->registry : nullptr;
        if (!m_camera || !m_image || !registry) return;
        const auto rect = m_image->processed_rect();
        if (rect.z <= 0 || rect.w <= 0) return;
        const auto size = m_camera->viewport_size();
        if (size.x <= 0 || size.y <= 0) return;
        const int left = std::max(rect.x, std::min(start.x, end.x));
        const int right = std::min(rect.x + rect.z, std::max(start.x, end.x));
        const int top = std::max(rect.y, std::min(start.y, end.y));
        const int bottom = std::min(rect.y + rect.w, std::max(start.y, end.y));
        if (!extend) m_context->select_entity(std::nullopt);
        else m_context->synchronize_selection();
        registry->cada<commons::transform>([&](std::uint32_t entity) {
            if (entity == m_camera->my_object || m_context->editor_camera_entity == entity ||
                m_context->hidden_entities.contains(entity) || m_context->locked_entities.contains(entity)) return;
            const auto transform = registry->get<commons::transform>(entity);
            const auto renderer = registry->get<commons::renderer>(entity);
            if (!transform || (renderer && !renderer->visible)) return;
            const auto projected = m_camera->world_to_screen(transform->get_world_position());
            if (!projected || projected->z < 0.f || projected->z > 1.f) return;
            const float x = rect.x + projected->x * rect.z / size.x;
            const float y = rect.y + projected->y * rect.w / size.y;
            if (x >= left && x <= right && y >= top && y <= bottom &&
                !m_context->selected_entities.contains(entity))
                m_context->select_entity(entity, true);
        });
    }

    std::optional<editor_context::entity_id> pick_at(const bgui::vec2i& mouse) const {
        const auto registry = m_context ? m_context->registry : nullptr;
        if (!m_camera || !m_image || !registry)
            return std::nullopt;
        const int x = m_image->processed_x();
        const int y = m_image->processed_y();
        const int width = m_image->processed_width();
        const int height = m_image->processed_height();

        if (width <= 0 || height <= 0 ||
            mouse.x < x || mouse.x >= x + width ||
            mouse.y < y || mouse.y >= y + height) {
            return std::nullopt;
        }

        m_camera->getViewMatrix();
        m_camera->obtProjectionMatrix();
        const auto ray = m_camera->point_to_ray({
            static_cast<float>(mouse.x - x) * m_camera->viewportFBO.x / width,
            static_cast<float>(mouse.y - y) * m_camera->viewportFBO.y / height
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

        float closest_bone = 10.f * bgui::get_global_scale();
        registry->cada<commons::transform>([&](uint32_t id) {
            const auto bone = registry->get<commons::transform>(id);
            if (bone->rig_joint_id.empty() || !commons::rig_entity_renderer(*bone) ||
                m_context->is_playing || m_context->hidden_entities.contains(id) || m_context->locked_entities.contains(id)) return;
            const auto tip = bone->get_world_position();
            const auto parent = registry->get<commons::transform>(bone->parent_id);
            const auto start = parent && !parent->rig_joint_id.empty() ? parent->get_world_position() : tip;
            const auto matrix = m_camera->projMatriz * m_camera->viewMatrix;
            const auto project = [&](const bgui::vec3& v) -> std::optional<glm::vec2> {
                const auto p = matrix * glm::vec4(v.x, v.y, v.z, 1);
                if (p.w <= 1.e-5f || std::abs(p.z) > p.w) return {};
                return glm::vec2{x + (p.x / p.w * .5f + .5f) * width, y + (.5f - p.y / p.w * .5f) * height};
            };
            const auto a = project(start), b = project(tip);
            if (!a || !b) return;
            const auto segment = *b - *a;
            const glm::vec2 point{mouse.x, mouse.y};
            const float length = glm::dot(segment, segment);
            const float t = length > 0 ? std::clamp(glm::dot(point - *a, segment) / length, 0.f, 1.f) : 0;
            const float distance = glm::length(point - (*a + segment * t));
            if (distance < closest_bone) { closest_bone = distance; hit_entity = id; }
        });
        return hit_entity;
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
    bool m_rectangle_enabled{false};
    bool m_box_drag{false};
    bool m_extend{false};
};

}
