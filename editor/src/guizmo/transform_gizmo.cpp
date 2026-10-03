#include "guizmo/transform_gizmo.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "components/camera.hpp"
#include "components/transform.hpp"

#include <bgui.hpp>

namespace {
    constexpr float pi = 3.14159265358979323846f;
    const std::array<bgui::vec4, 3> axis_colors{{
        {0.95f, 0.22f, 0.20f, 1.f},
        {0.34f, 0.84f, 0.36f, 1.f},
        {0.25f, 0.55f, 1.f, 1.f}
    }};
    const std::array<COMMONS_NS::fvec3, 3> axes{{
        {1.f, 0.f, 0.f},
        {0.f, 1.f, 0.f},
        {0.f, 0.f, 1.f}
    }};
    constexpr std::array<std::array<int, 2>, 3> rotation_planes{{
        {1, 2},
        {2, 0},
        {0, 1}
    }};

    float dot(const bgui::vec2& first, const bgui::vec2& second) {
        return first.x * second.x + first.y * second.y;
    }

    float length(const bgui::vec2& value) {
        return std::sqrt(dot(value, value));
    }

    std::array<COMMONS_NS::fvec3, 3> oriented_axes(const COMMONS_NS::fvec3& rotation) {
        auto matrix = glm::mat4(1.f);
        matrix = glm::rotate(matrix, glm::radians(rotation.x), glm::vec3(1.f, 0.f, 0.f));
        matrix = glm::rotate(matrix, glm::radians(rotation.y), glm::vec3(0.f, 1.f, 0.f));
        matrix = glm::rotate(matrix, glm::radians(rotation.z), glm::vec3(0.f, 0.f, 1.f));

        std::array<COMMONS_NS::fvec3, 3> result{};
        for (std::size_t index = 0; index < axes.size(); ++index) {
            const auto transformed = glm::mat3(matrix) * glm::vec3(
                axes[index].x, axes[index].y, axes[index].z);
            result[index] = {transformed.x, transformed.y, transformed.z};
        }
        return result;
    }

    bool clip_segment(
        const bgui::vec4i& bounds,
        bgui::vec2& start,
        bgui::vec2& end)
    {
        const float left = static_cast<float>(bounds.x);
        const float top = static_cast<float>(bounds.y);
        const float right = static_cast<float>(bounds.x + bounds.z);
        const float bottom = static_cast<float>(bounds.y + bounds.w);
        const float delta_x = end.x - start.x;
        const float delta_y = end.y - start.y;
        float first = 0.f;
        float last = 1.f;
        const auto clip_edge = [&first, &last](const float p, const float q) {
            if (std::abs(p) < 0.0001f)
                return q >= 0.f;
            const float amount = q / p;
            if (p < 0.f) {
                if (amount > last) return false;
                first = std::max(first, amount);
            } else {
                if (amount < first) return false;
                last = std::min(last, amount);
            }
            return true;
        };

        if (!clip_edge(-delta_x, start.x - left) || !clip_edge(delta_x, right - start.x) ||
            !clip_edge(-delta_y, start.y - top) || !clip_edge(delta_y, bottom - start.y))
            return false;

        const auto original_start = start;
        start = original_start + bgui::vec2{delta_x, delta_y} * first;
        end = original_start + bgui::vec2{delta_x, delta_y} * last;
        return true;
    }

    std::vector<bgui::vec2> clip_polygon(
        const bgui::vec4i& bounds,
        std::vector<bgui::vec2> polygon)
    {
        const std::array<float, 4> limits{
            static_cast<float>(bounds.x),
            static_cast<float>(bounds.x + bounds.z),
            static_cast<float>(bounds.y),
            static_cast<float>(bounds.y + bounds.w)
        };
        for (int edge = 0; edge < 4 && !polygon.empty(); ++edge) {
            std::vector<bgui::vec2> clipped;
            auto inside = [edge, &limits](const bgui::vec2& point) {
                if (edge == 0) return point.x >= limits[0];
                if (edge == 1) return point.x <= limits[1];
                if (edge == 2) return point.y >= limits[2];
                return point.y <= limits[3];
            };
            auto intersection = [edge, &limits](const bgui::vec2& start, const bgui::vec2& end) {
                if (edge < 2) {
                    const float amount = (limits[edge] - start.x) / (end.x - start.x);
                    return bgui::vec2{limits[edge], start.y + (end.y - start.y) * amount};
                }
                const int limit_index = edge;
                const float amount = (limits[limit_index] - start.y) / (end.y - start.y);
                return bgui::vec2{start.x + (end.x - start.x) * amount, limits[limit_index]};
            };

            auto previous = polygon.back();
            bool previous_inside = inside(previous);
            for (const auto& current : polygon) {
                const bool current_inside = inside(current);
                if (current_inside != previous_inside)
                    clipped.push_back(intersection(previous, current));
                if (current_inside)
                    clipped.push_back(current);
                previous = current;
                previous_inside = current_inside;
            }
            polygon = std::move(clipped);
        }
        return polygon;
    }

    void add_clipped_line(
        bgui::draw_list& draw_list,
        const bgui::vec4i& viewport,
        bgui::vec2 start,
        bgui::vec2 end,
        const float thickness)
    {
        const int inset = static_cast<int>(std::ceil(thickness * 0.5f));
        const bgui::vec4i bounds{
            viewport.x + inset,
            viewport.y + inset,
            std::max(0, viewport.z - inset * 2),
            std::max(0, viewport.w - inset * 2)
        };
        if (clip_segment(bounds, start, end))
            draw_list.add_line(start, end, thickness);
    }

    void add_clipped_polygon(
        bgui::draw_list& draw_list,
        const bgui::vec4i& viewport,
        std::vector<bgui::vec2> polygon)
    {
        polygon = clip_polygon(viewport, std::move(polygon));
        if (polygon.size() >= 3)
            draw_list.add_convexpolyfilled(polygon);
    }

    void add_clipped_polyline(
        bgui::draw_list& draw_list,
        const bgui::vec4i& viewport,
        const std::vector<bgui::vec2>& points,
        const float thickness,
        const bool closed)
    {
        if (points.size() < 2)
            return;
        for (std::size_t index = 1; index < points.size(); ++index)
            add_clipped_line(draw_list, viewport, points[index - 1], points[index], thickness);
        if (closed)
            add_clipped_line(draw_list, viewport, points.back(), points.front(), thickness);
    }

    float distance_to_segment(
        const bgui::vec2& point,
        const bgui::vec2& start,
        const bgui::vec2& end)
    {
        const auto segment = end - start;
        const float segment_length_squared = dot(segment, segment);
        if (segment_length_squared <= 0.0001f)
            return length(point - start);
        const float amount = std::clamp(dot(point - start, segment) / segment_length_squared, 0.f, 1.f);
        return length(point - (start + segment * amount));
    }

    bool project(
        const COMMONS_NS::camera& camera,
        const bgui::vec4i& viewport,
        const COMMONS_NS::fvec3& world,
        bgui::vec2& screen)
    {
        const auto clip = camera.projMatriz * camera.viewMatrix *
            glm::vec4(world.x, world.y, world.z, 1.f);
        if (clip.w <= 0.0001f || viewport.z <= 0 || viewport.w <= 0)
            return false;

        float ndc_x = clip.x/ clip.w;
        float ndc_y = clip.y/ clip.w;
        const float source_aspect = camera.viewportFBO.y > 0
            ? static_cast<float>(camera.viewportFBO.x) / camera.viewportFBO.y
            : static_cast<float>(viewport.z) / viewport.w;
        const float target_aspect = static_cast<float>(viewport.z) / viewport.w;
        if (source_aspect > target_aspect)
            ndc_x *= source_aspect / target_aspect;
        else if (source_aspect < target_aspect)
            ndc_y *= target_aspect / source_aspect;

        screen = {
            viewport.x + (ndc_x * 0.5f + 0.5f) * viewport.z,
            viewport.y + (0.5f - ndc_y * 0.5f) * viewport.w
        };
        return true;
    }

    bool key_pressed(const bgui::input_key key) {
        const auto& inputs = bgui::get_context().m_input_map;
        const auto found = inputs.find(key);
        return found != inputs.end() && found->second == bgui::input_action::press;
    }
}

bool editor::transform_gizmo::update(
    const COMMONS_NS::camera& camera,
    COMMONS_NS::transform* target,
    const bgui::vec4i& viewport,
    const bgui::vec2i& mouse_position,
    const bgui::vec2i& mouse_delta,
    const bool mouse_over_view,
    const bool left_just_pressed,
    const bool left_down)
{
    if (!target || viewport.z <= 0 || viewport.w <= 0) {
        m_dragging = false;
        m_active_axis = -1;
        return false;
    }

    if (mouse_over_view && !m_dragging) {
        if (key_pressed(bgui::input_key::g)) m_mode = mode::translate;
        if (key_pressed(bgui::input_key::r)) m_mode = mode::rotate;
        if (key_pressed(bgui::input_key::s)) m_mode = mode::scale;
    }
    if (m_dragging && !left_down) {
        m_dragging = false;
        m_active_axis = -1;
    }

    const auto position = target->get_position();
    if (!project(camera, viewport, position, m_center))
        return m_dragging;
    if (m_center.x < viewport.x || m_center.x > viewport.x + viewport.z ||
        m_center.y < viewport.y || m_center.y > viewport.y + viewport.w)
        return m_dragging && left_down;

    const auto axis_vectors = oriented_axes(target->get_rotation());
    std::array<bgui::vec2, 3> axis_ends{};
    std::array<bgui::vec2, 3> axis_directions{};
    std::array<float, 3> pixels_per_unit{};
    std::array<std::vector<bgui::vec2>, 3> rings;
    int hovered_axis = -1;
    float hovered_distance = 10.f;
    const bgui::vec2 mouse{
        static_cast<float>(mouse_position.x),
        static_cast<float>(mouse_position.y)
    };

    for (int axis_index = 0; axis_index < 3; ++axis_index) {
        bgui::vec2 unit_end{};
        if (!project(camera, viewport, position + axis_vectors[axis_index], unit_end))
            continue;

        const auto unit_screen_direction = unit_end - m_center;
        pixels_per_unit[axis_index] = length(unit_screen_direction);
        if (pixels_per_unit[axis_index] < 0.01f)
            continue;
        axis_directions[axis_index] = unit_screen_direction / pixels_per_unit[axis_index];
    }

    const auto center_clip = camera.projMatriz * camera.viewMatrix *
        glm::vec4(position.x, position.y, position.z, 1.f);
    const float pixels_per_world = viewport.w * std::abs(camera.projMatriz[1][1]) /
        (2.f * std::max(center_clip.w, 0.0001f));
    const float ring_radius = std::clamp(74.f / std::max(pixels_per_world, 0.01f), 0.02f, 10.f);

    for (int axis_index = 0; axis_index < 3; ++axis_index) {
        if (m_mode == mode::rotate) {
            const auto plane = rotation_planes[axis_index];
            auto& ring = rings[axis_index];
            float world_radius = ring_radius;
            for (int adjustment = 0; adjustment < 3; ++adjustment) {
                ring.clear();
                float maximum_screen_radius = 0.f;
                ring.reserve(48);
                for (int step = 0; step < 48; ++step) {
                    const float angle = 2.f * pi * static_cast<float>(step) / 48.f;
                    const auto point = position + axis_vectors[plane[0]] * (std::cos(angle) * world_radius) +
                        axis_vectors[plane[1]] * (std::sin(angle) * world_radius);
                    bgui::vec2 projected{};
                    if (!project(camera, viewport, point, projected)) {
                        ring.clear();
                        break;
                    }
                    maximum_screen_radius = std::max(maximum_screen_radius, length(projected - m_center));
                    ring.push_back(projected);
                }
                if (ring.size() != 48 || maximum_screen_radius < 0.01f)
                    break;
                world_radius = std::clamp(world_radius * (74.f / maximum_screen_radius), 0.02f, 10.f);
            }
            for (std::size_t point_index = 0; point_index < ring.size(); ++point_index) {
                const auto& start = ring[point_index];
                const auto& end = ring[(point_index + 1) % ring.size()];
                const float distance = distance_to_segment(mouse, start, end);
                if (mouse_over_view && distance < hovered_distance) {
                    hovered_distance = distance;
                    hovered_axis = axis_index;
                }
            }
        } else {
            if (pixels_per_unit[axis_index] < 0.01f)
                continue;
            const float world_length = std::clamp(82.f / pixels_per_unit[axis_index], 0.02f, 10.f);
            if (!project(camera, viewport, position + axis_vectors[axis_index] * world_length, axis_ends[axis_index]))
                continue;
            const float distance = distance_to_segment(mouse, m_center, axis_ends[axis_index]);
            if (mouse_over_view && distance < hovered_distance) {
                hovered_distance = distance;
                hovered_axis = axis_index;
            }
        }
    }

    if (left_just_pressed && hovered_axis >= 0) {
        m_active_axis = hovered_axis;
        m_dragging = true;
        m_drag_start_mouse = mouse;
        if (m_mode == mode::translate || m_mode == mode::scale) {
            m_drag_axis_screen = axis_directions[m_active_axis];
            m_drag_pixels_per_unit = pixels_per_unit[m_active_axis];
        }
        if (m_mode == mode::scale)
            m_start_scale = target->get_scale()[m_active_axis];
        if (m_mode == mode::rotate)
            m_last_mouse_angle = std::atan2(mouse.y - m_center.y, mouse.x - m_center.x);
    }

    if (m_dragging && left_down && m_active_axis >= 0) {
        if (m_mode == mode::translate && m_drag_pixels_per_unit > 0.01f) {
            const bgui::vec2 drag_delta{
                static_cast<float>(mouse_delta.x),
                static_cast<float>(mouse_delta.y)
            };
            const float amount = dot(drag_delta, m_drag_axis_screen) / m_drag_pixels_per_unit;
            if (!left_just_pressed)
                target->move(axis_vectors[m_active_axis] * amount);
        } else if (m_mode == mode::scale && m_drag_pixels_per_unit > 0.01f) {
            const float amount = dot(mouse - m_drag_start_mouse, m_drag_axis_screen) / 82.f;
            auto scale = target->get_scale();
            scale[m_active_axis] = std::max(0.001f, m_start_scale * (1.f + amount));
            target->set_scale(scale);
        } else if (m_mode == mode::rotate) {
            const float angle = std::atan2(mouse.y - m_center.y, mouse.x - m_center.x);
            float delta = angle - m_last_mouse_angle;
            if (delta > pi) delta -= 2.f * pi;
            if (delta < -pi) delta += 2.f * pi;
            COMMONS_NS::fvec3 rotation_delta{};
            rotation_delta[m_active_axis] = delta * 180.f / pi;
            target->rotate(rotation_delta);
            m_last_mouse_angle = angle;
        }
    }

    auto& draw_list = bgui::get_draw_list();
    for (int axis_index = 0; axis_index < 3; ++axis_index) {
        const bool highlighted = axis_index == hovered_axis || (m_dragging && axis_index == m_active_axis);
        auto color = axis_colors[axis_index];
        if (highlighted)
            color = {1.f, 0.86f, 0.28f, 1.f};
        draw_list.set_color(color);

        if (m_mode == mode::rotate) {
            if (!rings[axis_index].empty())
                add_clipped_polyline(
                    draw_list, viewport, rings[axis_index], highlighted ? 4.f : 2.5f, true);
            continue;
        }

        const auto end = axis_ends[axis_index];
        add_clipped_line(draw_list, viewport, m_center, end, highlighted ? 5.f : 3.5f);
        if (m_mode == mode::translate) {
            const auto direction = axis_directions[axis_index];
            const bgui::vec2 normal{-direction.y, direction.x};
            add_clipped_polygon(draw_list, viewport, {
                end,
                end - direction * 11.f + normal * 5.f,
                end - direction * 11.f - normal * 5.f
            });
        } else {
            add_clipped_polygon(draw_list, viewport, {
                end - bgui::vec2{5.f, 5.f},
                {end.x + 5.f, end.y - 5.f},
                end + bgui::vec2{5.f, 5.f},
                {end.x - 5.f, end.y + 5.f}
            });
        }
    }
    draw_list.set_color({1.f, 1.f, 1.f, 1.f});
    std::vector<bgui::vec2> center_marker;
    center_marker.reserve(16);
    for (int index = 0; index < 16; ++index) {
        const float angle = 2.f * pi * static_cast<float>(index) / 16.f;
        center_marker.push_back({
            m_center.x + std::cos(angle) * 4.f,
            m_center.y + std::sin(angle) * 4.f
        });
    }
    add_clipped_polygon(draw_list, viewport, std::move(center_marker));
    return m_dragging && left_down;
}