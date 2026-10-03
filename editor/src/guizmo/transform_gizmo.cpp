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

        float ndc_x = clip.x / clip.w;
        float ndc_y = clip.y / clip.w;
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
        const auto axis = axes[axis_index];
        bgui::vec2 unit_end{};
        if (!project(camera, viewport, position + axis, unit_end))
            continue;

        const auto unit_screen_direction = unit_end - m_center;
        pixels_per_unit[axis_index] = length(unit_screen_direction);
        if (pixels_per_unit[axis_index] < 0.01f)
            continue;
        axis_directions[axis_index] = unit_screen_direction / pixels_per_unit[axis_index];

        if (m_mode == mode::rotate) {
            const auto plane = rotation_planes[axis_index];
            const float average_scale = (pixels_per_unit[plane[0]] + pixels_per_unit[plane[1]]) * 0.5f;
            if (average_scale < 0.01f)
                continue;
            const float radius = std::clamp(72.f / average_scale, 0.02f, 10.f);
            auto& ring = rings[axis_index];
            ring.reserve(48);
            for (int step = 0; step < 48; ++step) {
                const float angle = 2.f * pi * static_cast<float>(step) / 48.f;
                const auto point = position + axes[plane[0]] * (std::cos(angle) * radius) +
                    axes[plane[1]] * (std::sin(angle) * radius);
                bgui::vec2 projected{};
                if (!project(camera, viewport, point, projected)) {
                    ring.clear();
                    break;
                }
                ring.push_back(projected);
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
            const float world_length = std::clamp(82.f / pixels_per_unit[axis_index], 0.02f, 10.f);
            if (!project(camera, viewport, position + axis * world_length, axis_ends[axis_index]))
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
                target->move(axes[m_active_axis] * amount);
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
            const auto plane = rotation_planes[m_active_axis];
            const auto first = axis_directions[plane[0]];
            const auto second = axis_directions[plane[1]];
            const float handedness = first.x * second.y - first.y * second.x;
            target->rotate(axes[m_active_axis] * (delta * (handedness < 0.f ? -1.f : 1.f) * 180.f / pi));
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
                draw_list.add_polyline(rings[axis_index], highlighted ? 4.f : 2.5f, true);
            continue;
        }

        const auto end = axis_ends[axis_index];
        draw_list.add_line(m_center, end, highlighted ? 5.f : 3.5f);
        if (m_mode == mode::translate) {
            const auto direction = axis_directions[axis_index];
            const bgui::vec2 normal{-direction.y, direction.x};
            draw_list.add_triangle(
                end,
                end - direction * 11.f + normal * 5.f,
                end - direction * 11.f - normal * 5.f
            );
        } else {
            draw_list.add_rect_filled(end - bgui::vec2{5.f, 5.f}, end + bgui::vec2{5.f, 5.f});
        }
    }
    draw_list.set_color({1.f, 1.f, 1.f, 1.f});
    draw_list.add_circle_filled(m_center, 4.f, 16);
    return m_dragging && left_down;
}